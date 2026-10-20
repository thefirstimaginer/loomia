//! Protocolo de rede do Loomia.
//!
//! Transporte: **WebSocket** (`ws://host:port/ws`), mensagens de texto contendo um
//! objeto JSON com o campo `"type"` (enum com tag interna `t`, via `serde`).
//!
//! Fluxo da sessão:
//!
//! ```text
//! cliente                                  servidor
//!   |-------- ClientMessage::Join ------------>|
//!   |<------- ServerMessage::Welcome ----------|  (id, mapa, jogadores, chat recente)
//!   |<------- ServerMessage::PlayerJoined -----|  (para os outros jogadores)
//!   |<------- ServerMessage::Snapshot (30Hz) --|
//!   |-------- ClientMessage::Input (30Hz) ---->|  (moveu, pulou, virou câmera)
//!   |-------- ClientMessage::Chat ------------>|
//!   |<------- ServerMessage::Chat -------------|
//!   |-------- ClientMessage::Respawn --------->|
//! ```
//!
//! O servidor simula a física a 30 tiques/s e faz *broadcast* de um snapshot com a
//! posição de todos os jogadores. O cliente prevê o seu movimento local e reconcilia.

use serde::{Deserialize, Serialize};

use crate::physics::InputState;
use crate::player::Appearance;
use crate::world::WorldMap;

/// Pacotes enviados pelo cliente ao servidor.
#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(tag = "type", rename_all = "camelCase")]
pub enum ClientMessage {
    /// Primeira mensagem da sessão: aparência do avatar.
    Join {
        appearance: Appearance,
    },
    /// Inputs de um tique de física.
    Input {
        state: InputState,
        #[serde(default)]
        camera_yaw: f32,
        #[serde(default)]
        camera_pitch: f32,
    },
    Chat {
        text: String,
    },
    Respawn,
    /// Pedido de ping (mede latência na HUD).
    Ping {
        client_time_ms: f64,
    },
}

/// Estado de um jogador dentro de um snapshot.
#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(rename_all = "camelCase")]
pub struct PlayerStateWire {
    pub id: u32,
    pub appearance: Appearance,
    /// [x, y, z] — posição dos pés.
    pub position: [f32; 3],
    pub yaw: f32,
    pub velocity: [f32; 3],
    pub on_ground: bool,
    /// `seq` do último input aplicado (o dono da sessão usa para reconciliação).
    pub ack: u32,
}

/// Pacotes enviados pelo servidor ao cliente.
#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(tag = "type", rename_all = "camelCase")]
pub enum ServerMessage {
    Welcome {
        player_id: u32,
        server_version: String,
        map: WorldMap,
        players: Vec<PlayerStateWire>,
        /// Últimas mensagens do chat, para o jogador já entrar contextualizado.
        recent_chat: Vec<(String, String)>,
        tick_rate: u32,
    },
    PlayerJoined {
        player: PlayerStateWire,
    },
    PlayerLeft {
        id: u32,
    },
    /// Snapshot autoritativo (todos os jogadores vivos).
    Snapshot {
        tick: u64,
        players: Vec<PlayerStateWire>,
    },
    Chat {
        from: String,
        text: String,
    },
    /// O jogador caiu do mapa — o servidor o reposicionou (também chega via snapshot).
    Respawned {
        position: [f32; 3],
    },
    /// Correção forçada do servidor (ex.: teleporte por anti-cheat ou respawn).
    Correction {
        position: [f32; 3],
        ack: u32,
    },
    Pong {
        client_time_ms: f64,
    },
    Error {
        message: String,
    },
}

pub const MAX_CHAT_LEN: usize = 140;
pub const PROTOCOL_VERSION: &str = "0.1.0";

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn protocol_roundtrip() {
        let msg = ClientMessage::Input {
            state: InputState {
                seq: 42,
                move_x: 0.5,
                move_z: -1.0,
                jump: true,
            },
            camera_yaw: 1.2,
            camera_pitch: -0.3,
        };
        let json = serde_json::to_string(&msg).unwrap();
        assert!(json.contains("\"type\":\"input\""));
        let back: ClientMessage = serde_json::from_str(&json).unwrap();
        match back {
            ClientMessage::Input { state, .. } => {
                assert_eq!(state.seq, 42);
                assert!(state.jump);
            }
            _ => panic!("tipo errado"),
        }
    }

    #[test]
    fn welcome_roundtrip_with_map() {
        let msg = ServerMessage::Welcome {
            player_id: 1,
            server_version: PROTOCOL_VERSION.into(),
            map: WorldMap::test_map(),
            players: vec![],
            recent_chat: vec![("Loomia".into(), "Bem-vindo!".into())],
            tick_rate: 30,
        };
        let json = serde_json::to_string(&msg).unwrap();
        let back: ServerMessage = serde_json::from_str(&json).unwrap();
        match back {
            ServerMessage::Welcome { map, .. } => assert!(map.blocks.len() > 10),
            _ => panic!("tipo errado"),
        }
    }
}
