//! Servidor do Loomia.
//!
//! Um único processo `axum`/`tokio` serve:
//!
//! * `GET /` — o frontend React (build de `loomia-frontend/dist`); rotas `/api`
//!   nunca caem no fallback do SPA.
//! * `GET /api/...` — API web (status, mundos, jogadores, LAUNCH do cliente).
//! * `GET /ws` — WebSocket do cliente 3D.
//!
//! A simulação autoritativa fica em [`game::Game`].

mod api;
mod game;
mod launcher;
mod log;
mod registry;

use std::{net::SocketAddr, path::PathBuf, sync::Arc, time::Duration};

use axum::{
    extract::{
        ws::{Message, WebSocket, WebSocketUpgrade},
        State,
    },
    response::IntoResponse,
    routing::{get, post},
    Json, Router,
};
use loomia_shared::{
    protocol::{ClientMessage, PROTOCOL_VERSION},
    vec::Vec3,
    world::WorldMap,
};
use serde_json::json;
use tokio::{
    net::TcpListener,
    sync::{broadcast, Notify},
};
use tower_http::{cors::CorsLayer, trace::TraceLayer};

use crate::{api::AppState, game::Game, launcher::Launcher};

/// Versão exibida na API/launcher.
pub const VERSION: &str = env!("CARGO_PKG_VERSION");

#[tokio::main]
async fn main() {
    let cfg = Config::from_env();

    // Prepara o cliente nativo em segundo plano (necessário para o LAUNCH).
    let launcher = Launcher::new(cfg.client_command.clone(), cfg.project_root.clone());
    let client_ready = Arc::new(Notify::new());
    tokio::spawn({
        let launcher = launcher.clone();
        let ready = client_ready.clone();
        async move {
            match launcher.ensure_built().await {
                Ok(()) => log::info(&format!("cliente pronto: {}", launcher.client_binary().display())),
                Err(err) => log::error(&format!("não foi possível preparar o cliente: {err}")),
            }
            ready.notify_waiters();
        }
    });

    let spawn = Vec3::from_slice(&WorldMap::test_map().spawn);
    let game = Game::new(cfg.worlds.clone(), cfg.bots, spawn, cfg.host.clone(), cfg.port);
    game.load_world(cfg.maps.clone(), client_ready.clone()).await;
    tokio::spawn(game.clone().run_tick_loop());

    let state = AppState { game: game.clone(), launcher: Arc::new(launcher), frontend_dir: cfg.frontend_dir.clone() };
    let app = build_router(state);

    let addr = SocketAddr::from(([0, 0, 0, 0], cfg.port));
    let listener = match TcpListener::bind(addr).await {
        Ok(listener) => listener,
        Err(err) => {
            eprintln!(
                "ERRO: não consegui abrir a porta {} ({err}).\n\
                 Dica: use LOOMIA_PORT=8081 para escolher outra porta.",
                cfg.port
            );
            std::process::exit(1);
        }
    };

    banner(&cfg, &game);

    let shutdown_game = game.clone();
    // `ConnectInfo` é usado por `/api/launch` e `/api/maps` para checar se a
    // requisição veio da máquina local; sem `into_make_service_with_connect_info`
    // esses handlers falham com "Missing request extension".
    axum::serve(
        listener,
        app.into_make_service_with_connect_info::<SocketAddr>(),
    )
        .with_graceful_shutdown(shutdown_signal(shutdown_game))
        .await
        .expect("servidor falhou");

    log::info("servidor encerrado");
}

fn build_router(state: AppState) -> Router {
    let frontend = tower_http::services::ServeDir::new(&state.frontend_dir).append_index_html_on_directories(true);

    Router::new()
        .route("/api/health", get(api::health))
        .route("/api/status", get(api::status))
        .route("/api/worlds", get(api::worlds))
        .route("/api/worlds/{id}", get(api::world))
        .route("/api/maps/{id}", get(api::map_file))
        .route("/api/maps", post(api::save_map))
        .route("/api/player", post(api::register_player))
        .route("/api/players", get(api::list_players))
        .route("/api/launch", post(api::launch))
        .route("/api/launch/status", get(api::launch_status))
        .route("/ws", get(ws_upgrade))
        .fallback_service(frontend)
        .layer(CorsLayer::permissive())
        .layer(TraceLayer::new_for_http())
        .with_state(state)
}

async fn ws_upgrade(
    ws: WebSocketUpgrade,
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
) -> impl IntoResponse {
    if let Err(err) = state.game.check_origin(headers.get("origin").and_then(|v| v.to_str().ok())) {
        return (axum::http::StatusCode::FORBIDDEN, Json(json!({ "error": err }))).into_response();
    }
    ws.on_upgrade(move |socket| handle_socket(socket, state.game))
}

/// Sessão WebSocket. Três fontes competem no `select!`:
/// * entrada do socket (JSON do cliente) → `game.handle_message`;
/// * `broadcast` global (snapshots/eventos) → enviado ao cliente;
/// * canal direcionado (`Welcome`, `Pong`) → enviado só a este cliente.
async fn handle_socket(mut socket: WebSocket, game: Arc<Game>) {
    let hello = json!({ "type": "hello", "version": PROTOCOL_VERSION, "server": VERSION });
    if socket.send(Message::Text(hello.to_string().into())).await.is_err() {
        return;
    }

    let (session_id, mut broadcast_rx, mut direct_rx) = match game.connect().await {
        Ok(pair) => pair,
        Err(err) => {
            let _ = socket
                .send(Message::Text(json!({ "type": "error", "message": err }).to_string().into()))
                .await;
            return;
        }
    };
    log::info(&format!("sessão {session_id} conectada ({})", game.presence_desc()));

    loop {
        tokio::select! {
            incoming = socket.recv() => {
                match incoming {
                    Some(Ok(Message::Text(text))) => {
                        match serde_json::from_str::<ClientMessage>(&text) {
                            Ok(msg) => game.handle_message(session_id, msg).await,
                            Err(err) => log::debug(&format!("mensagem inválida de {session_id}: {err}")),
                        }
                    }
                    Some(Ok(Message::Close(_))) | None => break,
                    Some(Ok(_)) => {}
                    Some(Err(_)) => break,
                }
            }
            event = broadcast_rx.recv() => {
                match event {
                    Ok(text) => {
                        if socket.send(Message::Text(text.into())).await.is_err() {
                            break;
                        }
                    }
                    Err(broadcast::error::RecvError::Lagged(skipped)) => {
                        log::warn(&format!("sessão {session_id} atrasada: {skipped} pacotes descartados"));
                    }
                    Err(broadcast::error::RecvError::Closed) => break,
                }
            }
            Some(text) = direct_rx.recv() => {
                if socket.send(Message::Text(text.into())).await.is_err() {
                    break;
                }
            }
        }
    }

    game.disconnect(session_id).await;
    log::info(&format!("sessão {session_id} desconectada ({})", game.presence_desc()));

}

/// Configuração vinda de variáveis de ambiente (com padrões de desenvolvimento).
pub struct Config {
    pub host: String,
    pub port: u16,
    pub bots: usize,
    pub worlds: Vec<api::WorldInfo>,
    pub maps: PathBuf,
    pub data_dir: PathBuf,
    pub frontend_dir: PathBuf,
    pub project_root: PathBuf,
    pub client_command: Option<String>,
}

impl Config {
    fn from_env() -> Self {
        let _ = WorldMap::test_map; // mapa de teste é carregado pelo Game
        let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        let project_root = manifest_dir.parent().map(PathBuf::from).unwrap_or_else(|| manifest_dir.clone());
        let cwd = std::env::current_dir().unwrap_or_else(|_| project_root.clone());
        let resolve = |name: &str, default: PathBuf| std::env::var(name).map(PathBuf::from).unwrap_or(default);

        let host = std::env::var("LOOMIA_HOST").unwrap_or_else(|_| "127.0.0.1".into());
        let port: u16 = std::env::var("LOOMIA_PORT")
            .or_else(|_| std::env::var("PORT"))
            .ok()
            .and_then(|p| p.parse().ok())
            .unwrap_or(8080);
        let bots = std::env::var("LOOMIA_BOTS").ok().and_then(|v| v.parse().ok()).unwrap_or(2);
        let data_dir = resolve("LOOMIA_DATA_DIR", project_root.join("data"));

        Self {
            host,
            port,
            bots,
            worlds: vec![api::WorldInfo {
                id: loomia_shared::DEFAULT_MAP_ID.to_string(),
                name: "Baseplate Clássica".to_string(),
                description: "O clássico estúdio de 2008: gramado, blocos coloridos e uma casinha."
                    .to_string(),
                max_players: 24,
                player_count: bots as u32,
            }],
            maps: resolve("LOOMIA_MAPS_DIR", data_dir.join("worlds")),
            data_dir,
            frontend_dir: std::env::var("LOOMIA_FRONTEND_DIR")
                .map(PathBuf::from)
                .unwrap_or_else(|_| cwd.join("loomia-frontend").join("dist")),
            project_root,
            client_command: std::env::var("LOOMIA_CLIENT_COMMAND").ok(),
        }
    }
}

fn banner(cfg: &Config, game: &Game) {
    println!();
    println!("  +----------------------------------------------+");
    println!("  |             L O O M I A   server             |");
    println!("  +----------------------------------------------+");
    println!("   Versao     : {VERSION}");
    println!("   Web / API  : http://{}:{}", cfg.host, cfg.port);
    println!("   WebSocket  : {}", game.ws_url());
    println!("   Frontend   : {}", cfg.frontend_dir.display());
    println!("   Mundos     : {}", cfg.maps.display());
    println!("   Tiques     : {} Hz (autoritativo)", game.tick_rate());
    println!("   Bots       : {}", cfg.bots);
    println!("   Ctrl+C para encerrar.");
    println!();
}

async fn shutdown_signal(game: Arc<Game>) {
    let ctrl_c = async {
        let _ = tokio::signal::ctrl_c().await;
    };
    #[cfg(unix)]
    let terminate = async {
        match tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate()) {
            Ok(mut sig) => {
                sig.recv().await;
            }
            Err(_) => std::future::pending::<()>().await,
        }
    };
    #[cfg(not(unix))]
    let terminate = std::future::pending::<()>();

    tokio::select! {
        _ = ctrl_c => {},
        _ = terminate => {},
    }
    game.shutdown();
    tokio::time::sleep(Duration::from_millis(60)).await;
}
