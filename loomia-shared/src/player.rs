//! Aparência dos jogadores (o "avatar" estilo 2008: camisa, calça, pele e chapéu).

use serde::{Deserialize, Serialize};

fn default_username() -> String {
    "Player".to_string()
}
fn default_body_color() -> [u8; 3] {
    [235, 190, 25]
}
fn default_shirt_color() -> [u8; 3] {
    [15, 120, 190]
}
fn default_pants_color() -> [u8; 3] {
    [35, 75, 45]
}

/// Cores do avatar. Os nomes seguem o vocabulário do Roblox clássico.
#[derive(Serialize, Deserialize, Clone, Debug, PartialEq)]
pub struct Appearance {
    #[serde(default = "default_username")]
    pub username: String,
    /// Pele (cabeça, braços).
    #[serde(default = "default_body_color")]
    pub body_color: [u8; 3],
    #[serde(default = "default_shirt_color")]
    pub shirt_color: [u8; 3],
    #[serde(default = "default_pants_color")]
    pub pants_color: [u8; 3],
    /// Nenhum chapéu é `None` (campo opcional).
    #[serde(default)]
    pub hat: Option<Hat>,
}

#[derive(Serialize, Deserialize, Clone, Copy, Debug, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Hat {
    Cap,
    TopHat,
    Propeller,
    Crown,
}

impl Default for Appearance {
    fn default() -> Self {
        Self {
            username: default_username(),
            body_color: default_body_color(),
            shirt_color: default_shirt_color(),
            pants_color: default_pants_color(),
            hat: None,
        }
    }
}

impl Appearance {
    /// Gera um avatar aleatório (servidor) para quem não enviou aparência.
    pub fn random_for(username: &str, seed: u32) -> Self {
        const BODIES: [[u8; 3]; 6] = [
            [235, 190, 25],
            [224, 175, 140],
            [140, 90, 60],
            [245, 210, 170],
            [120, 200, 140],
            [190, 190, 200],
        ];
        const SHIRTS: [[u8; 3]; 8] = [
            [15, 120, 190],
            [190, 40, 40],
            [230, 230, 230],
            [70, 70, 70],
            [240, 160, 20],
            [110, 60, 160],
            [40, 160, 90],
            [250, 120, 170],
        ];
        const PANTS: [[u8; 3]; 6] = [
            [35, 75, 45],
            [30, 30, 40],
            [90, 70, 50],
            [60, 60, 120],
            [120, 120, 120],
            [160, 130, 90],
        ];
        let mut s = seed.wrapping_mul(2654435761) ^ 0x9E37_79B9;
        let mut next = || {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            s as usize
        };
        let hats = [None, None, Some(Hat::Cap), Some(Hat::TopHat), Some(Hat::Propeller), Some(Hat::Crown)];
        Self {
            username: username.to_string(),
            body_color: BODIES[next() % BODIES.len()],
            shirt_color: SHIRTS[next() % SHIRTS.len()],
            pants_color: PANTS[next() % PANTS.len()],
            hat: hats[next() % hats.len()],
        }
    }

    /// Sanitiza dados vindos da rede (tamanho do nome, caracteres de controle).
    pub fn sanitized(mut self) -> Self {
        self.username = self
            .username
            .chars()
            .filter(|c| !c.is_control())
            .take(20)
            .collect();
        if self.username.trim().is_empty() {
            self.username = default_username();
        }
        self
    }
}
