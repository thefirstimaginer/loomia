//! Estado autoritativo do mundo e loop de simulação.
//!
//! Cada sessão WebSocket tem:
//! * um canal `mpsc` de **entrada** (cliente → jogo), consumido no loop de tique;
//! * um canal `mpsc` de **saída** (jogo → cliente), para mensagens direcionadas
//!   (`Welcome`, `Pong`).
//!
//! Além disso existe um `broadcast` global para o que todos precisam ver: snapshots
//! (serializados uma única vez por tique) e eventos como chat/entrada/saída.
//!
//! O loop de tique roda a 30 Hz: aplica inputs com a física compartilhada, move os
//! bots de demonstração, respawna quem caiu do mapa e publica o snapshot.

use std::{
    collections::HashMap,
    sync::{
        atomic::{AtomicBool, AtomicU64, Ordering},
        Arc,
    },
    time::Duration,
};

use loomia_shared::{
    physics::{fell_out_of_world, move_character, CharacterState, InputState},
    player::Appearance,
    protocol::{ClientMessage, PlayerStateWire, ServerMessage, MAX_CHAT_LEN, PROTOCOL_VERSION},
    vec::Vec3,
    world::WorldMap,
};
use tokio::sync::{broadcast, mpsc, Mutex, Notify};

use crate::{api::WorldInfo, log, registry::Registry};

/// Frequência de simulação/replicação (Hz).
pub const TICK_RATE: u32 = 30;
pub const TICK_DT: f32 = 1.0 / TICK_RATE as f32;
/// Máximo de inputs acumulados por jogador por tique (anti-flood).
const MAX_QUEUED_INPUTS: usize = 6;
/// Histórico de chat enviado no `Welcome`.
const CHAT_HISTORY: usize = 30;
/// Limite de sessões simultâneas (anti-DoS básico).
const MAX_SESSIONS: usize = 128;

pub struct Player {
    pub id: u32,
    pub appearance: Appearance,
    pub state: CharacterState,
    pub ack: u32,
    pub queued_inputs: Vec<InputState>,
    /// Câmera do jogador remoto (recebida nos inputs).
    pub camera_yaw: f32,
    pub camera_pitch: f32,
    /// `None` para bots.
    pub session: Option<u64>,
    pub bot: Option<BotBrain>,
}

pub struct Session {
    pub id: u64,
    pub player_id: Option<u32>,
    pub sender: mpsc::UnboundedSender<ClientMessage>,
    pub outbox: mpsc::UnboundedSender<String>,
}

/// Bot de demonstração: anda em círculo pela baseplate, pula e conversa.
pub struct BotBrain {
    angle: f32,
    radius: f32,
    speed: f32,
    jump_timer: f32,
    chat_timer: f32,
    next_chat: usize,
}

const BOT_CHAT: &[&str] = &[
    "Alguém quer construir uma torre?",
    "essa baseplate é clássica demais",
    "cuidado com a rampa de tijolos!",
    "meu avatar ficou ótimo",
    "quem caiu do mapa?",
    "BrickBattle quando?",
    "aperte T para conversar",
];

impl BotBrain {
    fn new(center: Vec3, radius: f32, phase: f32) -> Self {
        let _ = center;
        Self {
            angle: phase,
            radius,
            speed: 0.35 + phase.cos().abs() * 0.25,
            jump_timer: 2.0 + phase,
            chat_timer: 25.0 + phase * 3.0,
            next_chat: (phase.abs() as usize) % BOT_CHAT.len(),
        }
    }

    fn think(&mut self, dt: f32) -> InputState {
        // Velocidade angular constante em uma órbita de raio `radius`.
        // Com a velocidade linear v = speed (unidades/s), omega = v / radius.
        let omega = self.speed / self.radius.max(0.5);
        self.angle += dt * omega;
        let tangent = Vec3::new(-self.angle.sin(), 0.0, self.angle.cos()).normalized_horizontal();
        self.jump_timer -= dt;
        let jump = if self.jump_timer <= 0.0 {
            self.jump_timer = 6.0 + self.angle.sin().abs() * 5.0;
            true
        } else {
            false
        };
        // A velocidade tangencial máxima é WALK_SPEED, então o input é o unitário.
        InputState { seq: 0, move_x: tangent.x, move_z: tangent.z, jump }
    }

    fn take_chat_line(&mut self, dt: f32) -> Option<&'static str> {
        self.chat_timer -= dt;
        if self.chat_timer <= 0.0 {
            self.chat_timer = 40.0 + self.angle.cos().abs() * 25.0;
            let line = BOT_CHAT[self.next_chat % BOT_CHAT.len()];
            self.next_chat += 1;
            Some(line)
        } else {
            None
        }
    }
}

struct Inner {
    map: WorldMap,
    players: HashMap<u32, Player>,
    sessions: HashMap<u64, Session>,
    chat_log: Vec<(String, String)>,
    next_player_id: u32,
    next_session_id: u64,
    tick: u64,
}

/// Handle compartilhado do jogo (API HTTP + sessões WS).
pub struct Game {
    inner: Mutex<Inner>,
    outbox: broadcast::Sender<String>,
    worlds: Mutex<Vec<WorldInfo>>,
    /// Pasta de onde o `.loom` ativo foi carregado (None antes do `load_world`).
    maps_dir: Mutex<Option<std::path::PathBuf>>,
    registry: Registry,
    host: String,
    port: u16,
    started_at: std::time::Instant,
    running: AtomicBool,
    pub total_connections: AtomicU64,
    pub total_chat_messages: AtomicU64,
}

impl Game {
    pub fn new(worlds: Vec<WorldInfo>, bots: usize, spawn: Vec3, host: String, port: u16) -> Arc<Self> {
        let (outbox, _) = broadcast::channel(512);
        let game = Arc::new(Self {
            inner: Mutex::new(Inner {
                map: WorldMap::test_map(),
                players: HashMap::new(),
                sessions: HashMap::new(),
                chat_log: Vec::new(),
                next_player_id: 1,
                next_session_id: 1,
                tick: 0,
            }),
            outbox,
            worlds: Mutex::new(worlds),
            maps_dir: Mutex::new(None),
            registry: Registry::new(),
            host,
            port,
            started_at: std::time::Instant::now(),
            running: AtomicBool::new(true),
            total_connections: AtomicU64::new(0),
            total_chat_messages: AtomicU64::new(0),
        });
        game.spawn_bots(bots, spawn);
        game
    }

    /// Carrega/grava o mundo em disco (`.loom`) e posiciona todos no spawn.
    /// Espera (por até 2,5 s) o cliente terminar de compilar, para que ao abrir, o
    /// jogo já encontre o servidor no ar.
    pub async fn load_world(self: &Arc<Self>, maps_dir: std::path::PathBuf, client_built: Arc<Notify>) {
        let _ = tokio::time::timeout(Duration::from_millis(2500), client_built.notified()).await;

        let mut map = WorldMap::test_map();
        let path = maps_dir.join(format!("{}.loom", map.id));
        match tokio::fs::read_to_string(&path).await {
            Ok(text) => match serde_json::from_str::<WorldMap>(&text) {
                Ok(loaded) => {
                    log::info(&format!(
                        "mundo '{}' carregado de {} ({} blocos)",
                        loaded.name,
                        path.display(),
                        loaded.blocks.len()
                    ));
                    map = loaded;
                }
                Err(err) => log::warn(&format!("falha ao ler {}: {err} (usando mapa de teste)", path.display())),
            },
            Err(_) => {
                if let Some(dir) = path.parent() {
                    let _ = tokio::fs::create_dir_all(dir).await;
                }
                if let Ok(text) = serde_json::to_string_pretty(&map) {
                    let _ = tokio::fs::write(&path, text).await;
                    log::info(&format!("mapa de teste salvo em {}", path.display()));
                }
            }
        }

        {
            let mut inner = self.inner.lock().await;
            log::info(&format!("mundo ativo: {} ({} blocos)", map.name, map.blocks.len()));
            inner.map = map;
        }
        *self.maps_dir.lock().await = Some(maps_dir);
        self.reset_all_to_spawn().await;
    }

    /// Re-lê o `.loom` do disco (usado pelo Studio depois de salvar) e reabre
    /// as sessões para o novo mundo. Os jogadores voltam ao spawn.
    pub async fn reload_world(self: &Arc<Self>) {
        let Some(dir) = self.maps_dir.lock().await.clone() else { return };
        let current = self.inner.lock().await.map.id.clone();
        let path = dir.join(format!("{current}.loom"));
        let Ok(text) = tokio::fs::read_to_string(&path).await else {
            log::warn(&format!("reload: não consegui ler {}", path.display()));
            return;
        };
        match serde_json::from_str::<WorldMap>(&text) {
            Ok(loaded) => {
                log::info(&format!("mundo '{}' recarregado ({} blocos)", loaded.name, loaded.blocks.len()));
                self.inner.lock().await.map = loaded;
                self.reset_all_to_spawn().await;
            }
            Err(err) => log::warn(&format!("reload: {} inválido: {err}", path.display())),
        }
    }

    /// Grava o mapa (JSON já validado pelo chamador) e coloca todos no spawn.
    pub async fn save_world(&self, id: String, text: String) -> Result<(), String> {
        let dir = self.maps_dir.lock().await.clone().ok_or("mapa não carregado")?;
        tokio::fs::create_dir_all(&dir).await.map_err(|e| e.to_string())?;
        let path = dir.join(format!("{id}.loom"));
        tokio::fs::write(&path, text).await.map_err(|e| e.to_string())?;
        log::info(&format!("mapa salvo em {}", path.display()));
        Ok(())
    }

    /// Dados exatos do arquivo `.loom` em disco (para o editor baixar o mapa).
    pub async fn world_file(&self) -> Option<(std::path::PathBuf, String)> {
        let dir = self.maps_dir.lock().await.clone()?;
        let id = self.inner.lock().await.map.id.clone();
        let path = dir.join(format!("{id}.loom"));
        match tokio::fs::read_to_string(&path).await {
            Ok(text) => Some((path, text)),
            Err(_) => {
                let map = self.map_clone().await;
                serde_json::to_string_pretty(&map).ok().map(|text| (path, text))
            }
        }
    }

    pub fn tick_rate(&self) -> u32 {
        TICK_RATE
    }

    pub fn url(&self) -> String {
        format!("http://{}:{}", self.host, self.port)
    }

    pub fn ws_url(&self) -> String {
        format!("ws://{}:{}/ws", self.host, self.port)
    }

    pub fn uptime_secs(&self) -> u64 {
        self.started_at.elapsed().as_secs()
    }

    pub fn presence_desc(&self) -> String {
        match self.inner.try_lock() {
            Ok(inner) => format!("{} jogadores online", inner.players.len()),
            Err(_) => "ocupado".to_string(),
        }
    }

    pub async fn player_count(&self) -> usize {
        self.inner.lock().await.players.len()
    }

    pub async fn session_count(&self) -> usize {
        self.inner.lock().await.sessions.len()
    }

    pub async fn map_summary(&self) -> (String, usize) {
        let inner = self.inner.lock().await;
        (inner.map.name.clone(), inner.map.blocks.len())
    }

    pub async fn worlds(&self) -> Vec<WorldInfo> {
        let count = self.inner.lock().await.players.len() as u32;
        let mut worlds = self.worlds.lock().await;
        for w in worlds.iter_mut() {
            w.player_count = count;
        }
        worlds.clone()
    }

    pub async fn map_clone(&self) -> WorldMap {
        self.inner.lock().await.map.clone()
    }

    pub async fn online_players(&self) -> Vec<PlayerStateWire> {
        let inner = self.inner.lock().await;
        wire_players(&inner)
    }

    pub async fn register_player(&self, appearance: Appearance) -> Appearance {
        let appearance = appearance.sanitized();
        self.registry.upsert(appearance.clone()).await;
        appearance
    }

    pub async fn known_players(&self) -> Vec<Appearance> {
        self.registry.all().await
    }

    pub fn check_origin(&self, origin: Option<&str>) -> Result<(), String> {
        let dev = std::env::var("LOOMIA_DEV").is_ok();
        match origin {
            None => Ok(()), // cliente nativo não envia Origin
            Some(origin) => {
                let local = origin == "null"
                    || origin.contains("localhost")
                    || origin.contains("127.0.0.1")
                    || origin.contains(&format!(":{}", self.port));
                if local || dev {
                    Ok(())
                } else {
                    Err(format!("origem não permitida: {origin}"))
                }
            }
        }
    }

    pub fn shutdown(&self) {
        self.running.store(false, Ordering::SeqCst);
    }

    // ----------------------------------------------------------------------
    // Sessões
    // ----------------------------------------------------------------------

    /// Cria a sessão e devolve (id, receptor do broadcast, receptor direcionado).
    pub async fn connect(
        &self,
    ) -> Result<(u64, broadcast::Receiver<String>, mpsc::UnboundedReceiver<String>), String> {
        let mut inner = self.inner.lock().await;
        if inner.sessions.len() >= MAX_SESSIONS {
            return Err("servidor lotado, tente novamente".into());
        }
        let id = inner.next_session_id;
        inner.next_session_id += 1;
        let (tx, _rx) = mpsc::unbounded_channel();
        let (out_tx, out_rx) = mpsc::unbounded_channel();
        inner.sessions.insert(id, Session { id, player_id: None, sender: tx, outbox: out_tx });
        self.total_connections.fetch_add(1, Ordering::Relaxed);
        Ok((id, self.outbox.subscribe(), out_rx))
    }

    pub async fn session_sender(&self, session_id: u64) -> Option<mpsc::UnboundedSender<ClientMessage>> {
        let inner = self.inner.lock().await;
        inner.sessions.get(&session_id).map(|s| s.sender.clone())
    }

    pub async fn disconnect(&self, session_id: u64) {
        let removed = {
            let mut inner = self.inner.lock().await;
            let session = inner.sessions.remove(&session_id);
            let player = session
                .as_ref()
                .and_then(|s| s.player_id)
                .and_then(|pid| inner.players.remove(&pid))
                .map(|p| (p.id, p.appearance.username));
            (session.is_some(), player)
        };
        if let (_, Some((id, name))) = removed {
            self.publish(&ServerMessage::PlayerLeft { id });
            self.push_chat("Loomia", &format!("{name} saiu da experiência.")).await;
        }
    }

    pub async fn handle_message(&self, session_id: u64, msg: ClientMessage) {
        match msg {
            ClientMessage::Join { appearance } => self.join(session_id, appearance).await,
            ClientMessage::Input { state, camera_yaw, camera_pitch } => {
                let mut inner = self.inner.lock().await;
                let pid = inner.sessions.get(&session_id).and_then(|s| s.player_id);
                if let Some(player) = pid.and_then(|pid| inner.players.get_mut(&pid)) {
                    // A câmera é usada no futuro para orientar o avatar em terceira
                    // pessoa sem input de movimento; guardamos desde já.
                    player.camera_yaw = camera_yaw;
                    player.camera_pitch = camera_pitch.clamp(-1.4, 1.4);
                    if player.queued_inputs.len() < MAX_QUEUED_INPUTS {
                        player.queued_inputs.push(state);
                    }
                }
            }
            ClientMessage::Chat { text } => self.chat(session_id, text).await,
            ClientMessage::Respawn => {
                let pid = {
                    let inner = self.inner.lock().await;
                    inner.sessions.get(&session_id).and_then(|s| s.player_id)
                };
                if let Some(pid) = pid {
                    self.respawn(pid).await;
                }
            }
            ClientMessage::Ping { client_time_ms } => {
                self.send_direct(session_id, &ServerMessage::Pong { client_time_ms }).await;
            }
        }
    }

    async fn send_direct(&self, session_id: u64, msg: &ServerMessage) {
        if let Ok(text) = serde_json::to_string(msg) {
            let inner = self.inner.lock().await;
            if let Some(session) = inner.sessions.get(&session_id) {
                let _ = session.outbox.send(text);
            }
        }
    }

    async fn join(&self, session_id: u64, appearance: Appearance) {
        let appearance = appearance.sanitized();
        let (id, welcome, joined_wire, username) = {
            let mut inner = self.inner.lock().await;

            let Some(session) = inner.sessions.get(&session_id) else { return };
            if session.player_id.is_some() {
                return;
            }

            let id = inner.next_player_id;
            inner.next_player_id += 1;
            let spawn = Vec3::from_slice(&inner.map.spawn);
            let jitter = (id as f32 * 1.7).sin() * 1.5;
            let mut state =
                CharacterState::new(Vec3::new(spawn.x + jitter, spawn.y + 0.2, spawn.z + jitter * 0.5), 0.0);
            state.on_ground = true;

            let welcome_players = wire_players(&inner);
            let player = Player {
                id,
                appearance: appearance.clone(),
                state,
                ack: 0,
                queued_inputs: Vec::new(),
                camera_yaw: 0.0,
                camera_pitch: 0.0,
                session: Some(session_id),
                bot: None,
            };
            let joined_wire = wire_player(&player);

            let welcome = ServerMessage::Welcome {
                player_id: id,
                server_version: format!("{} / protocolo {}", crate::VERSION, PROTOCOL_VERSION),
                map: inner.map.clone(),
                players: welcome_players,
                recent_chat: inner.chat_log.clone(),
                tick_rate: TICK_RATE,
            };

            inner.players.insert(id, player);
            if let Some(s) = inner.sessions.get_mut(&session_id) {
                s.player_id = Some(id);
            }
            (id, welcome, joined_wire, appearance.username)
        };

        self.send_direct(session_id, &welcome).await;
        self.publish(&ServerMessage::PlayerJoined { player: joined_wire });
        let presence = self.presence_desc();
        log::info(&format!("{username} entrou (id {id}, sessão {session_id}) - {presence}"));
        self.push_chat("Loomia", &format!("{username} entrou na experiência.")).await;
    }

    async fn chat(&self, session_id: u64, text: String) {
        let text: String = text.chars().filter(|c| !c.is_control()).take(MAX_CHAT_LEN).collect();
        let text = text.trim().to_string();
        if text.is_empty() {
            return;
        }
        let from = {
            let inner = self.inner.lock().await;
            inner
                .sessions
                .get(&session_id)
                .and_then(|s| s.player_id)
                .and_then(|pid| inner.players.get(&pid))
                .map(|p| p.appearance.username.clone())
        };
        let Some(from) = from else { return };
        self.total_chat_messages.fetch_add(1, Ordering::Relaxed);
        log::debug(&format!("chat <{from}> {text}"));
        self.push_chat(&from, &text).await;
    }

    async fn push_chat(&self, from: &str, text: &str) {
        let msg = ServerMessage::Chat { from: from.to_string(), text: text.to_string() };
        {
            let mut inner = self.inner.lock().await;
            inner.chat_log.push((from.to_string(), text.to_string()));
            if inner.chat_log.len() > CHAT_HISTORY {
                inner.chat_log.remove(0);
            }
        }
        self.publish(&msg);
    }

    pub async fn respawn(&self, player_id: u32) {
        let correction = {
            let mut inner = self.inner.lock().await;
            let spawn = Vec3::from_slice(&inner.map.spawn);
            inner.players.get_mut(&player_id).map(|p| {
                let jitter = (player_id as f32 * 2.3).sin() * 1.2;
                p.state =
                    CharacterState::new(Vec3::new(spawn.x + jitter, spawn.y + 0.6, spawn.z + jitter), p.state.yaw);
                p.queued_inputs.clear();
                (p.state.position.to_array(), p.ack)
            })
        };
        if let Some((position, ack)) = correction {
            self.publish(&ServerMessage::Correction { position, ack });
        }
    }

    fn spawn_bots(self: &Arc<Self>, count: usize, spawn: Vec3) {
        let center = Vec3::new(spawn.x, spawn.y, spawn.z - 2.0);
        // `Game::new` roda dentro da runtime do Tokio; `blocking_lock` panica nesse contexto.
        let mut inner = self.inner.try_lock().unwrap_or_else(|_| {
            // Nunca deve acontecer na construção, mas garante que não travamos/panicamos.
            panic!("spawn_bots chamado com o lock do jogo já ocupado")
        });
        for i in 0..count {
            let phase = i as f32 / count.max(1) as f32 * std::f32::consts::TAU;
            let radius = 6.0 + i as f32 * 1.6;
            let id = inner.next_player_id;
            inner.next_player_id += 1;
            let appearance = Appearance::random_for(&format!("Loomian{}", i + 1), id);
            let state = CharacterState::new(
                Vec3::new(center.x + radius * phase.cos(), spawn.y + 0.2, center.z + radius * phase.sin()),
                0.0,
            );
            inner.players.insert(
                id,
                Player {
                    id,
                    appearance,
                    state,
                    ack: 0,
                    queued_inputs: Vec::new(),
                    camera_yaw: 0.0,
                    camera_pitch: 0.0,
                    session: None,
                    bot: Some(BotBrain::new(center, radius, phase)),
                },
            );
        }
    }

    async fn reset_all_to_spawn(&self) {
        let mut inner = self.inner.lock().await;
        let spawn = Vec3::from_slice(&inner.map.spawn);
        let center = Vec3::new(spawn.x, spawn.y, spawn.z - 2.0);
        let mut ids: Vec<u32> = inner.players.keys().copied().collect();
        ids.sort_unstable();
        for (i, id) in ids.into_iter().enumerate() {
            let radius = 6.0 + i as f32 * 1.6;
            let phase = i as f32 * std::f32::consts::TAU / 6.0;
            if let Some(p) = inner.players.get_mut(&id) {
                if p.bot.is_some() {
                    p.bot = Some(BotBrain::new(center, radius, phase));
                }
                p.state = CharacterState::new(
                    Vec3::new(center.x + radius * phase.cos(), spawn.y + 0.2, center.z + radius * phase.sin()),
                    0.0,
                );
                p.state.on_ground = true;
                p.queued_inputs.clear();
            }
        }
    }

    // ----------------------------------------------------------------------
    // Simulação
    // ----------------------------------------------------------------------

    pub async fn run_tick_loop(self: Arc<Self>) {
        let mut interval = tokio::time::interval(Duration::from_secs_f64(TICK_DT as f64));
        interval.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
        log::info(&format!("loop de jogo iniciado a {TICK_RATE} Hz"));

        loop {
            interval.tick().await;
            if !self.running.load(Ordering::SeqCst) {
                break;
            }
            self.tick().await;
        }
    }

    pub async fn tick(&self) {
        let chat_lines: Vec<(String, String)> = {
            let mut inner = self.inner.lock().await;
            inner.tick += 1;
            let map = inner.map.clone();
            let spawn = Vec3::from_slice(&map.spawn);
            let mut chat = Vec::new();
            let mut respawn_ids = Vec::new();

            for player in inner.players.values_mut() {
                if let Some(brain) = player.bot.as_mut() {
                    let input = brain.think(TICK_DT);
                    player.state = move_character(player.state, input, &map.blocks, TICK_DT);
                    if let Some(line) = brain.take_chat_line(TICK_DT) {
                        chat.push((player.appearance.username.clone(), line.to_string()));
                    }
                }
                // Inputs do cliente: cada um é um tique de física (o cliente envia
                // 30/s; os que chegarem em atraso são aplicados em sequência).
                for input in player.queued_inputs.drain(..) {
                    player.state = move_character(player.state, input, &map.blocks, TICK_DT);
                    player.ack = input.seq;
                }
                // Exija que a simulação acompanhe o tempo real: cerca de 1 tique
                // de física por frame do cliente; se a rede atrasar, aplicamos o
                // último input repetido para não "congelar".
                if fell_out_of_world(&player.state, &map.blocks) {
                    respawn_ids.push(player.id);
                }
            }

            for id in respawn_ids {
                if let Some(p) = inner.players.get_mut(&id) {
                    let jitter = (id as f32 * 2.3).sin() * 1.2;
                    let yaw = p.state.yaw;
                    p.state =
                        CharacterState::new(Vec3::new(spawn.x + jitter, spawn.y + 0.6, spawn.z + jitter), yaw);
                    p.queued_inputs.clear();
                    log::debug(&format!("jogador {id} caiu do mapa; respawn"));
                }
            }
            chat
        };

        for (from, text) in chat_lines {
            self.push_chat(&from, &text).await;
        }

        self.broadcast_snapshot().await;
    }

    /// Serializa o snapshot **uma vez** e publica para todos os clientes.
    pub async fn broadcast_snapshot(&self) {
        let json = {
            let inner = self.inner.lock().await;
            if inner.players.is_empty() {
                return;
            }
            let msg = ServerMessage::Snapshot { tick: inner.tick, players: wire_players(&inner) };
            serde_json::to_string(&msg).unwrap_or_default()
        };
        if !json.is_empty() {
            let _ = self.outbox.send(json);
        }
    }

    fn publish(&self, msg: &ServerMessage) {
        if let Ok(text) = serde_json::to_string(msg) {
            let _ = self.outbox.send(text);
        }
    }
}

fn wire_player(p: &Player) -> PlayerStateWire {
    PlayerStateWire {
        id: p.id,
        appearance: p.appearance.clone(),
        position: p.state.position.to_array(),
        yaw: p.state.yaw,
        velocity: p.state.velocity.to_array(),
        on_ground: p.state.on_ground,
        ack: p.ack,
    }
}

fn wire_players(inner: &Inner) -> Vec<PlayerStateWire> {
    let mut players: Vec<_> = inner.players.values().map(wire_player).collect();
    players.sort_by_key(|p| p.id);
    players
}
