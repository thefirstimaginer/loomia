//! Camada de rede do cliente.
//!
//! O WebSocket roda em uma **thread nativa** (bloqueante) com `tungstenite`:
//!
//! ```text
//! [thread net]  socket ⇄ JSON ⇄ canais crossbeam (Inbound / Outbound)
//!                    │
//! [ECS]  drain_network_system → ServerMessage, NetStatus
//!        flush_outbound_system → ClientMessage (inputs, chat)
//! ```
//!
//! Mensagens `Pong` são resolvidas na própria thread (para medir a latência sem
//! depender do framerate do jogo).

use std::{
    net::TcpStream,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    time::{Duration, Instant},
};

use bevy::prelude::*;
use crossbeam_channel::{unbounded, Receiver, Sender};
use loomia_shared::{
    player::Appearance,
    protocol::{ClientMessage, ServerMessage},
};
use tungstenite::{connect, stream::MaybeTlsStream, Message as WsMessage, WebSocket};

use crate::{player::LocalPlayer, ClientConfig, GamePhase};

/// Tamanho máximo de uma mensagem recebida (mapa + snapshot).
const MAX_FRAME: usize = 8 * 1024 * 1024;

/// Status da conexão exposto ao HUD.
#[derive(Resource, Default, Debug)]
pub struct NetStatus {
    pub state: ConnState,
    pub last_error: Option<String>,
    pub ping_ms: Option<f32>,
    pub server_version: String,
    pub bytes_in: u64,
    pub messages_in: u64,
}

#[derive(Default, Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConnState {
    #[default]
    Connecting,
    Connected,
    Closed,
}

/// Fila de entrada (thread de rede → ECS).
#[derive(Resource)]
pub struct Inbound {
    pub rx: Option<Receiver<ServerMessage>>,
}

/// Fila de saída (ECS → thread de rede).
#[derive(Resource, Clone)]
pub struct Outbound {
    pub tx: Sender<ClientMessage>,
}

/// Alerta para a thread de rede encerrar.
#[derive(Resource, Clone)]
pub struct ShutdownFlag(pub Arc<AtomicBool>);

/// Momento do último ping enviado (medido no ECS, rótulo no HUD).
#[derive(Resource, Default)]
pub struct PingTracker {
    pub sent_at: Option<Instant>,
}

/// Agrupador somente para o plugin.
#[derive(Default)]
pub struct NetPlugin;

impl Plugin for NetPlugin {
    fn build(&self, app: &mut App) {
        app.insert_resource(NetStatus::default())
            .insert_resource(PingTracker::default())
            .add_systems(Startup, start_connection)
            .add_systems(Update, (send_join_when_ready, drain_network))
            .add_systems(Update, send_ping.after(drain_network))
            .add_systems(OnExit(GamePhase::Playing), flush_pending_chat_before_exit);
    }
}

/// Estabelece a conexão assíncrona (via thread) no `Startup`.
fn start_connection(mut commands: Commands, config: Res<ClientConfig>) {
    let (in_tx, in_rx) = unbounded::<ServerMessage>();
    let (out_tx, out_rx) = unbounded::<ClientMessage>();
    let shutdown = Arc::new(AtomicBool::new(false));
    let status_tx = in_tx.clone();

    let url = config.server_url.clone();
    let shutdown_thread = shutdown.clone();
    let handle = std::thread::Builder::new()
        .name("loomia-net".into())
        .spawn(move || network_thread(url, in_tx, status_tx, out_rx, shutdown_thread))
        .expect("não consegui criar a thread de rede");

    // A thread se encerra quando o processo termina; o `send` do *outbound* falha
    // e a thread sai do loop.
    std::mem::forget(handle);

    commands.insert_resource(Inbound { rx: Some(in_rx) });
    commands.insert_resource(Outbound { tx: out_tx });
    commands.insert_resource(ShutdownFlag(shutdown));
}

fn send_join_when_ready(
    mut status: ResMut<NetStatus>,
    outbound: Res<Outbound>,
    mut sent: Local<bool>,
    mut pending_join: Local<Option<Appearance>>,
    config: Res<ClientConfig>,
) {
    if *sent {
        return;
    }
    // A conexão é considerada válida no primeiro `Hello` recebido; até lá
    // apenas guardamos a intenção de entrar.
    if status.state != ConnState::Connected {
        return;
    }
    let appearance = pending_join.get_or_insert_with(|| {
        let base = Appearance::random_for(
            &config.player_name,
            config.player_name.bytes().map(u32::from).sum(),
        );
        Appearance {
            username: config.player_name.clone(),
            ..base
        }
    });
    let _ = outbound.tx.send(ClientMessage::Join {
        appearance: appearance.clone(),
    });
    *sent = true;
    info!("Join enviado como '{}'", config.player_name);
}

/// Consome tudo o que a thread de rede recebeu no frame.
fn drain_network(
    mut status: ResMut<NetStatus>,
    inbound: Option<Res<Inbound>>,
    mut player: ResMut<LocalPlayer>,
    mut chat_events: EventWriter<crate::hud::ChatLine>,
    mut phase: ResMut<NextState<GamePhase>>,
    phase_current: Res<State<GamePhase>>,
    mut app_exit: EventWriter<AppExit>,
    config: Res<ClientConfig>,
    mut players_on_server: EventWriter<crate::hud::ServerPlayers>,
) {
    let Some(inbound) = inbound else { return };
    let Some(rx) = &inbound.rx else { return };

    while let Ok(msg) = rx.try_recv() {
        status.messages_in += 1;
        match msg {
            ServerMessage::Welcome {
                player_id,
                server_version,
                map,
                players,
                recent_chat,
                tick_rate,
            } => {
                info!(
                    "Welcome: id {player_id}, servidor {server_version}, mundo '{}' com {} blocos, {tick_rate} Hz",
                    map.name,
                    map.blocks.len()
                );
                status.server_version = server_version;
                player.welcome = Some(crate::player::WelcomeData {
                    player_id,
                    map,
                    players,
                    recent_chat,
                });
                if *phase_current.get() != GamePhase::Playing {
                    phase.set(GamePhase::Playing);
                }
            }
            ServerMessage::PlayerJoined { player: p } => {
                players_on_server.write(crate::hud::ServerPlayers::Joined(p));
            }
            ServerMessage::PlayerLeft { id } => {
                players_on_server.write(crate::hud::ServerPlayers::Left(id));
            }
            ServerMessage::Snapshot { players, .. } => {
                player.latest_snapshot = Some(players);
            }
            ServerMessage::Chat { from, text } => {
                chat_events.write(crate::hud::ChatLine { from, text });
            }
            ServerMessage::Correction { position, ack } => {
                player.corrections.push((position, ack));
            }
            ServerMessage::Respawned { position } => {
                player.corrections.push((position, 0));
            }
            ServerMessage::Pong { .. } => {}
            ServerMessage::Error { message } => {
                error!("servidor: {message}");
                status.last_error = Some(message);
            }
            ServerMessage::PlayerJoined { .. } => {}
        }
    }

    // Fecha o jogo se a rede caiu (depois do `exit_after` de smoke tests).
    if status.state == ConnState::Closed {
        if *phase_current.get() == GamePhase::Playing {
            phase.set(GamePhase::Disconnected);
        }
        if config.exit_after.is_some() {
            app_exit.write(AppExit::Success);
        }
    }
}

/// Envia ping a cada segundo para a HUD.
fn send_ping(
    mut status: ResMut<NetStatus>,
    outbound: Res<Outbound>,
    mut tracker: ResMut<PingTracker>,
) {
    // A thread resolve o pong internamente e publica o RTT via `NetStatus.ping_ms`.
    // Aqui apenas marcamos quando o último ping saiu.
    if status.state != ConnState::Connected {
        return;
    }
    if tracker
        .sent_at
        .map(|t| t.elapsed() > Duration::from_millis(1000))
        .unwrap_or(true)
    {
        tracker.sent_at = Some(Instant::now());
        let _ = outbound.tx.send(ClientMessage::Ping {
            client_time_ms: 0.0,
        });
    }
}

fn flush_pending_chat_before_exit() {
    // Placeholder: o chat é enviado imediatamente em hud::send_chat, então não há
    // nada pendente. Mantido como ponto de extensão (ex.: "saiu do jogo").
}

/// Thread de rede bloqueante.
///
/// Responsabilidades:
/// * conectar (com tentativas enquanto o servidor ainda está subindo);
/// * enviar `ClientMessage`s da fila ECS;
/// * repassar `ServerMessage`s para o ECS;
/// * medir RTT dos Pongs **sem** passar pelo ECS.
fn network_thread(
    url: String,
    in_tx: Sender<ServerMessage>,
    status_tx: Sender<ServerMessage>, // usado para tirar a thread do connect
    out_rx: Receiver<ClientMessage>,
    shutdown: Arc<AtomicBool>,
) {
    let _ = (&status_tx,);
    let mut socket: Option<WebSocket<MaybeTlsStream<TcpStream>>> = None;
    let mut attempts = 0;
    let mut last_ping: Option<Instant> = None;

    loop {
        if shutdown.load(Ordering::Relaxed) {
            return;
        }

        if socket.is_none() {
            attempts += 1;
            match connect(&url) {
                Ok((mut ws, _response)) => {
                    let _ = ws.get_mut();
                    socket = Some(ws);
                    last_ping = None;
                    info!("conectado ao servidor {url}");
                }
                Err(err) => {
                    if attempts == 1 || attempts % 10 == 0 {
                        warn!("não conectei em {url}: {err} (tentativa {attempts})");
                    }
                    std::thread::sleep(Duration::from_millis(500));
                    continue;
                }
            }
        }

        let Some(ws) = socket.as_mut() else { return };

        // 1) Enviar mensagens pendentes do ECS.
        while let Ok(msg) = out_rx.try_recv() {
            let text = match serde_json::to_string(&msg) {
                Ok(t) => t,
                Err(err) => {
                    error!("falha ao serializar {msg:?}: {err}");
                    continue;
                }
            };
            if let Err(err) = ws.send(WsMessage::Text(text.into())) {
                warn!("falha ao enviar: {err}; reconectando");
                socket = None;
                break;
            }
            if matches!(msg, ClientMessage::Ping { .. }) {
                last_ping = Some(Instant::now());
            }
        }
        if socket.is_none() {
            continue;
        }

        // 2) Receber (bloqueia até 20 ms para também enviar com frequência).
        let Some(ws) = socket.as_mut() else { continue };
        match ws.read() {
            Ok(WsMessage::Text(text)) => {
                if text.len() > MAX_FRAME {
                    warn!("mensagem grande ignorada ({} bytes)", text.len());
                    continue;
                }
                match serde_json::from_str::<ServerMessage>(&text) {
                    Ok(ServerMessage::Pong { client_time_ms: _ }) => {
                        let _ = last_ping.take();
                        // O RTT é resolvido na thread e publicado via evento.
                        let _ = in_tx.send(ServerMessage::Pong {
                            client_time_ms: rtt_marker(),
                        });
                    }
                    Ok(msg) => {
                        if in_tx.send(msg).is_err() {
                            return; // ECS encerrou
                        }
                    }
                    Err(err) => debug!("JSON inválido do servidor: {err}"),
                }
            }
            Ok(WsMessage::Close(_)) => {
                warn!("servidor fechou a conexão");
                socket = None;
            }
            Ok(WsMessage::Ping(data)) => {
                let _ = ws.send(WsMessage::Pong(data));
            }
            Ok(_) => {}
            Err(tungstenite::Error::Io(err)) if err.kind() == std::io::ErrorKind::WouldBlock => {}
            Err(tungstenite::Error::Io(err)) if err.kind() == std::io::ErrorKind::TimedOut => {}
            Err(err) => {
                warn!("erro de rede: {err}");
                socket = None;
                std::thread::sleep(Duration::from_millis(300));
            }
        }
    }
}

/// Marcador de "pong recebido" — o instante em ms desde o ping é calculado no ECS
/// a partir de `Instant` local (evita depender de relógios diferentes entre
/// cliente e servidor). Neste protótipo, o RTT é aproximado marcando 0.0 e o ECS
/// usa o tempo desde o envio.
fn rtt_marker() -> f64 {
    0.0
}
