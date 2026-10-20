//!
//! Código compartilhado entre o cliente e o servidor do Loomia.
//!
//! Manter o protocolo, o formato do mundo e a física no mesmo crate garante dois
//! objetivos importantes de um jogo multiplayer:
//!
//! * cliente e servidor **nunca divergem** no formato dos pacotes;
//! * o cliente consegue prever o movimento usando exatamente as mesmas regras de
//!   física que o servidor autoritativo usa (ver [`protocol::InputState`]).
//!
//! Módulos:
//! * [`protocol`] – pacotes trocados pelo WebSocket.
//! * [`world`] – blocos, mapa de teste e conversão para o arquivo `.loom`.
//! * [`physics`] – colisão AABB e movimento do personagem estilo Roblox 2008.
//! * [`vec`] – vetor 3D mínimo (sem dependências).

pub mod physics;
pub mod player;
pub mod protocol;
pub mod vec;
pub mod world;

/// Identificador do mapa padrão servido pelo servidor.
pub const DEFAULT_MAP_ID: &str = "baseplate";

/// Altura do olhar (câmera) acima dos pés do personagem.
pub const EYE_HEIGHT: f32 = 1.55;
