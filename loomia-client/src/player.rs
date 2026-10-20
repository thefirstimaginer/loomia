//! Controle do jogador local: câmera, input, previsão e reconciliação.
//!
//! **Previsão do cliente**: a cada 1/30 s o cliente gera um [`InputState`], envia
//! ao servidor e *imediatamente* aplica a mesma física compartilhada
//! (`loomia_shared::physics::move_character`) — o personagem responde na hora,
//! mesmo com 100 ms de latência.
//!
//! **Reconciliação**: quando um snapshot chega, o cliente compara a posição
//! autoritativa com a prevista. Se a diferença for grande (parede, queda,
//! respawn), ele adota o estado do servidor; se for pequena, corrige suavemente.
//!
//! **Câmera**: órbita em terceira pessoa (botão direito ou esquerdo arrasta,
//! roda do mouse aproxima, `V` alterna primeira pessoa). `R` respawna.

use bevy::{
    input::mouse::{MouseMotion, MouseWheel},
    prelude::*,
    window::PrimaryWindow,
};

use loomia_shared::{
    physics::{move_character, CharacterState, InputState},
    player::Appearance,
    protocol::{ClientMessage, PlayerStateWire},
    vec::Vec3 as SVec3,
    world::WorldMap,
};

use crate::{net::Outbound, ClientConfig, GamePhase};

/// Estado previsível do personagem local (espelha `CharacterState`).
#[derive(Clone, Debug, Default)]
pub struct LocalPlayerState {
    pub position: SVec3,
    pub velocity: SVec3,
    pub yaw: f32,
    pub on_ground: bool,
}

impl From<CharacterState> for LocalPlayerState {
    fn from(state: CharacterState) -> Self {
        Self { position: state.position, velocity: state.velocity, yaw: state.yaw, on_ground: state.on_ground }
    }
}

impl LocalPlayerState {
    fn to_character(&self) -> CharacterState {
        CharacterState { position: self.position, velocity: self.velocity, yaw: self.yaw, on_ground: self.on_ground }
    }
}

#[derive(Resource)]
pub struct LocalPlayer {
    pub welcome: Option<WelcomeData>,
    pub predicted: LocalPlayerState,
    pub latest_snapshot: Option<Vec<PlayerStateWire>>,
    pub corrections: Vec<([f32; 3], u32)>,
    pub seq: u32,
    pub blocks: Vec<loomia_shared::world::Block>,
    pub send_accumulator: f32,
    pub autoplay_clock: f32,
    pub first_person: bool,
    /// `true` quando o mundo foi montado e a posição inicial foi definida.
    pub ready: bool,
}

impl Default for LocalPlayer {
    fn default() -> Self {
        Self {
            welcome: None,
            predicted: LocalPlayerState::default(),
            latest_snapshot: None,
            corrections: Vec::new(),
            seq: 0,
            blocks: Vec::new(),
            send_accumulator: 0.0,
            autoplay_clock: 0.0,
            first_person: false,
            ready: false,
        }
    }
}

impl LocalPlayer {
    pub fn player_id(&self) -> Option<u32> {
        self.welcome.as_ref().map(|w| w.player_id)
    }
}

/// Dados do `Welcome` guardados para montar o mundo.
#[derive(Resource, Debug)]
pub struct WelcomeData {
    pub player_id: u32,
    pub map: WorldMap,
    pub players: Vec<PlayerStateWire>,
    pub recent_chat: Vec<(String, String)>,
}

/// Órbita da câmera em relação ao personagem.
#[derive(Component, Clone, Copy, Debug)]
pub struct CameraOrbit {
    pub yaw: f32,
    pub pitch: f32,
    pub distance: f32,
}

impl CameraOrbit {
    /// Deslocamento da câmera em relação ao ponto de foco.
    pub fn offset(&self) -> Vec3 {
        let horizontal = self.distance * self.pitch.cos();
        Vec3::new(horizontal * self.yaw.sin(), -self.distance * self.pitch.sin(), horizontal * self.yaw.cos())
    }
}

/// Aparência do jogador local (enviada no `Join` pela thread de rede).
#[derive(Resource)]
pub struct LocalAppearance(pub Appearance);

pub struct PlayerPlugin;

impl Plugin for PlayerPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<LocalPlayer>()
            .add_systems(Startup, setup_local_appearance)
            .add_systems(Update, initialize_from_welcome)
            .add_systems(
                Update,
                (drag_to_orbit, apply_corrections, predict_and_send_input, update_camera)
                    .chain()
                    .run_if(in_state(GamePhase::Playing))
                    .run_if(|local: Res<LocalPlayer>| local.ready),
            );
    }
}

fn setup_local_appearance(mut commands: Commands, config: Res<ClientConfig>) {
    let seed: u32 = config.player_name.bytes().fold(7u32, |acc, b| acc.wrapping_mul(31).wrapping_add(b as u32));
    let appearance = config
        .appearance
        .clone()
        .unwrap_or_else(|| Appearance { username: config.player_name.clone(), ..Appearance::random_for(&config.player_name, seed) });
    commands.insert_resource(LocalAppearance(appearance));
}

/// Monta o mundo e a posição inicial quando o `Welcome` chega.
fn initialize_from_welcome(mut local: ResMut<LocalPlayer>, mut cache: ResMut<crate::avatar::AvatarCache>) {
    let Some(welcome) = local.welcome.take() else { return };

    local.blocks = welcome.map.blocks.clone();
    let spawn = SVec3::from_slice(&welcome.map.spawn);
    let me = welcome.players.iter().find(|p| p.id == welcome.player_id);
    let initial = me.map(|p| SVec3::from_slice(&p.position)).unwrap_or(spawn);

    local.predicted = LocalPlayerState {
        position: initial,
        velocity: SVec3::ZERO,
        yaw: me.map(|p| p.yaw).unwrap_or(0.0),
        on_ground: true,
    };
    local.ready = true;

    // Jogadores que já estavam no servidor entram no cache de avatares.
    for player in &welcome.players {
        cache.latest.insert(player.id, player.clone());
    }

    info!(
        "mundo pronto: '{}', {} blocos, jogador local id {}",
        welcome.map.name,
        welcome.map.blocks.len(),
        welcome.player_id
    );

    local.welcome = Some(welcome);
}

fn drag_to_orbit(
    mouse_buttons: Res<ButtonInput<MouseButton>>,
    mut motion: EventReader<MouseMotion>,
    mut wheel: EventReader<MouseWheel>,
    mut orbit: Query<&mut CameraOrbit>,
    mut local: ResMut<LocalPlayer>,
    keyboard: Res<ButtonInput<KeyCode>>,
    windows: Query<&Window, With<PrimaryWindow>>,
    outbound: Res<Outbound>,
) {
    let mut delta = Vec2::ZERO;
    for ev in motion.read() {
        delta += ev.delta;
    }
    let mut scroll = 0.0;
    for ev in wheel.read() {
        scroll += ev.y;
    }

    // Sem chat aberto (o HUD controla), arrastar gira a câmera.
    let ui_typing = windows.single().map(|w| w.cursor_options.grab_mode != bevy::window::CursorGrabMode::None).unwrap_or(false);
    let rotating = !ui_typing && (mouse_buttons.pressed(MouseButton::Right) || mouse_buttons.pressed(MouseButton::Left));

    if let Ok(mut orbit) = orbit.single_mut() {
        if rotating && delta != Vec2::ZERO {
            orbit.yaw -= delta.x * 0.005;
            orbit.pitch = (orbit.pitch - delta.y * 0.005).clamp(-1.25, 0.6);
        }
        if scroll.abs() > 0.0 && !ui_typing {
            orbit.distance = if local.first_person { 0.01 } else { (orbit.distance - scroll * 1.1).clamp(3.0, 24.0) };
        }

        if keyboard.just_pressed(KeyCode::KeyV) {
            local.first_person = !local.first_person;
            orbit.distance = if local.first_person { 0.01 } else { 9.5 };
        }
    }

    if keyboard.just_pressed(KeyCode::KeyR) {
        let _ = outbound.tx.send(ClientMessage::Respawn);
    }
}

/// Aplica correções do servidor (respawn/teleporte) imediatamente.
fn apply_corrections(mut local: ResMut<LocalPlayer>) {
    let corrections = std::mem::take(&mut local.corrections);
    for (position, _ack) in corrections {
        let target = SVec3::from_slice(&position);
        debug!("correção do servidor: {position:?}");
        local.predicted.position = target;
        local.predicted.velocity = SVec3::ZERO;
        local.predicted.on_ground = false;
    }
}

/// Gera inputs a 30 Hz, prevê localmente e envia ao servidor.
fn predict_and_send_input(
    time: Res<Time>,
    keyboard: Res<ButtonInput<KeyCode>>,
    orbit_query: Query<&CameraOrbit>,
    mut local: ResMut<LocalPlayer>,
    outbound: Res<Outbound>,
    config: Res<ClientConfig>,
) {
    let dt = time.delta_secs().min(0.1);

    // Reconciliação com o snapshot mais recente.
    if let Some(snapshot) = local.latest_snapshot.take() {
        if let Some(me) = local.player_id().and_then(|id| snapshot.iter().find(|p| p.id == id)).cloned() {
            let authoritative = SVec3::from_slice(&me.position);
            let drift = local.predicted.position - authoritative;
            let big = drift.length_horizontal() > 1.5 || (local.predicted.position.y - authoritative.y).abs() > 1.5;
            let ack_lag = local.seq.saturating_sub(me.ack);

            if big && ack_lag < 15 {
                local.predicted.position = authoritative;
                local.predicted.velocity = SVec3::from_slice(&me.velocity);
                local.predicted.on_ground = me.on_ground;
            } else if drift.length_horizontal() > 0.2 {
                local.predicted.position = local.predicted.position.lerp(authoritative, 0.12);
            }
        }
    }

    // Direção desejada relativa à câmera.
    let camera_yaw = orbit_query.single().map(|o| o.yaw).unwrap_or(0.0);
    let (mut forward, mut right) = (0.0f32, 0.0f32);
    if keyboard.pressed(KeyCode::KeyW) || keyboard.pressed(KeyCode::ArrowUp) {
        forward += 1.0;
    }
    if keyboard.pressed(KeyCode::KeyS) || keyboard.pressed(KeyCode::ArrowDown) {
        forward -= 1.0;
    }
    if keyboard.pressed(KeyCode::KeyD) || keyboard.pressed(KeyCode::ArrowRight) {
        right += 1.0;
    }
    if keyboard.pressed(KeyCode::KeyA) || keyboard.pressed(KeyCode::ArrowLeft) {
        right -= 1.0;
    }
    if config.autoplay {
        local.autoplay_clock += dt;
        forward = 1.0;
        right = (local.autoplay_clock * 0.6).sin();
    }

    // Câmera olha para -Z quando yaw = 0:
    //   frente = (-sin(yaw), 0, -cos(yaw))   |   direita = (cos(yaw), 0, -sin(yaw))
    let world_x = right * camera_yaw.cos() + forward * -camera_yaw.sin();
    let world_z = right * -camera_yaw.sin() + forward * -camera_yaw.cos();
    let jump = keyboard.pressed(KeyCode::Space);

    // Envia a 30 Hz, exatamente 1 tique de física por input.
    local.send_accumulator += dt;
    let tick = crate::TICK_DT;
    let mut steps = 0;
    while local.send_accumulator >= tick && steps < 4 {
        local.send_accumulator -= tick;
        steps += 1;
        local.seq = local.seq.wrapping_add(1);
        let input = InputState { seq: local.seq, move_x: world_x, move_z: world_z, jump };

        let next = move_character(local.predicted.to_character(), input, &local.blocks, tick);
        local.predicted = LocalPlayerState::from(next);

        let _ = outbound.tx.send(ClientMessage::Input { state: input, camera_yaw, camera_pitch: 0.0 });
    }
}

fn update_camera(
    local: Res<LocalPlayer>,
    mut camera: Query<(&CameraOrbit, &mut Transform)>,
) {
    let Ok((orbit, mut transform)) = camera.single_mut() else { return };

    let focus = Vec3::new(
        local.predicted.position.x,
        local.predicted.position.y + loomia_shared::EYE_HEIGHT,
        local.predicted.position.z,
    );
    let desired = focus + orbit.offset();
    transform.translation = if local.first_person { desired } else { transform.translation.lerp(desired, 0.35) };
    transform.look_at(focus, Vec3::Y);
}
