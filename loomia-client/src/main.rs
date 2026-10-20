//! Cliente 3D do Loomia (Bevy).
//!
//! Módulos:
//! * [`args`] — argumentos de linha de comando (`--server`, `--name`, `--world`).
//! * [`net`] — WebSocket em thread própria e canal para o ECS.
//! * [`loading`] — janela "Carregando Loomia, Aguarde!" enquanto conecta.
//! * [`world_render`] — instancia os blocos do mapa recebido no `Welcome`.
//! * [`avatar`] — constrói e anima os personagens (estilo 2008).
//! * [`player`] — câmera, input, previsão local e reconciliação.
//! * [`hud`] — chat, lista de jogadores e diagnósticos (ping/fps).
//! * [`scene`] — câmera, luzes e céu.

mod args;
mod avatar;
mod hud;
mod loading;
mod net;
mod player;
mod scene;
mod world_render;

use bevy::{
    prelude::*,
    window::{PresentMode, WindowResolution},
};

use crate::{args::LaunchOptions, net::NetPlugin};

/// Frequência dos inputs enviados ao servidor (igual ao tick do servidor).
pub const TICK_DT: f32 = 1.0 / 30.0;

/// Fases do jogo (Bevy `States`).
#[derive(States, Default, Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum GamePhase {
    /// Tela "Carregando Loomia, Aguarde!" até o `Welcome` chegar.
    #[default]
    Loading,
    /// Mundo carregado; o usuário joga.
    Playing,
    /// Conexão caiu.
    Disconnected,
}

/// Configuração de execução vinda dos argumentos.
#[derive(Resource, Debug, Clone)]
pub struct ClientConfig {
    pub server_url: String,
    pub player_name: String,
    pub world: Option<String>,
    /// `--autoplay` faz o avatar andar sozinho (usado em testes automatizados).
    pub autoplay: bool,
    /// `--exit-after N` encerra o cliente após N segundos (smoke tests).
    pub exit_after: Option<f32>,
    /// Aparência escolhida no site (enviada pelo launcher).
    pub appearance: Option<loomia_shared::player::Appearance>,
}

impl Default for ClientConfig {
    fn default() -> Self {
        Self {
            server_url: "ws://127.0.0.1:8080/ws".into(),
            player_name: "Loomian".into(),
            world: None,
            autoplay: false,
            exit_after: None,
            appearance: None,
        }
    }
}

fn main() {
    let opts: LaunchOptions = args::parse();
    let config = ClientConfig {
        server_url: opts.server,
        player_name: opts.name,
        world: opts.world,
        autoplay: opts.autoplay,
        exit_after: opts.exit_after,
        appearance: opts.appearance,
    };

    App::new()
        .insert_resource(ClearColor(Color::srgb_u8(150, 200, 240)))
        .insert_resource(config)
        .add_plugins(
            DefaultPlugins
                .set(WindowPlugin {
                    primary_window: Some(Window {
                        title: "Loomia".into(),
                        resolution: WindowResolution::new(1280.0, 720.0),
                        present_mode: PresentMode::AutoVsync,
                        ..default()
                    }),
                    ..default()
                })
                .set(ImagePlugin::default_nearest()),
        )
        .init_state::<GamePhase>()
        .add_plugins((
            NetPlugin,
            loading::LoadingPlugin,
            scene::ScenePlugin,
            world_render::WorldRenderPlugin,
            avatar::AvatarPlugin,
            player::PlayerPlugin,
            hud::HudPlugin,
        ))
        .add_systems(Startup, utilities::log_startup)
        .run();
}

mod utilities {
    use super::*;

    pub fn log_startup(config: Res<ClientConfig>) {
        info!(
            "Loomia client {} conectando em {}",
            env!("CARGO_PKG_VERSION"),
            config.server_url
        );
    }
}
