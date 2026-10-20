//! Mapa de blocos: lista de peças com posição, tamanho, cor e material.
//!
//! O formato é intencionalmente simples (uma lista de blocos) e é serializado em
//! JSON com a extensão `.loom` — é o equivalente "moderno" de um arquivo `.rbxl`
//! do Roblox clássico. O servidor envia o mesmo mapa para todos os clientes e o
//! usa para a colisão autoritativa.

use serde::{Deserialize, Serialize};

use crate::vec::Vec3;

/// Material de um bloco. Apenas a aparência muda no cliente.
#[derive(Serialize, Deserialize, Clone, Copy, Debug, PartialEq, Eq)]
#[serde(rename_all = "snake_case")]
pub enum Material {
    Plastic,
    Grass,
    Wood,
    Brick,
    Stone,
    Neon,
}

impl Material {
    /// Brilho emitido (0.0 = opaco). Usado pelo cliente para materiais neon.
    pub fn emissive(self) -> f32 {
        match self {
            Material::Neon => 1.6,
            _ => 0.0,
        }
    }
}

/// Uma peça do mundo. `position` é o **centro** do bloco.
#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct Block {
    pub position: [f32; 3],
    pub size: [f32; 3],
    /// Cor em RGB (0-255), mantendo o formato simples de editar à mão.
    pub color: [u8; 3],
    pub material: Material,
    /// Blocos com `collidable = false` são apenas decoração.
    #[serde(default = "default_true")]
    pub collidable: bool,
}

fn default_true() -> bool {
    true
}

impl Block {
    pub fn new(position: [f32; 3], size: [f32; 3], color: [u8; 3], material: Material) -> Self {
        Self {
            position,
            size,
            color,
            material,
            collidable: true,
        }
    }

    /// Bloco cúbico de 1x1x1 com o centro dado.
    pub fn cube(x: f32, y: f32, z: f32, color: [u8; 3], material: Material) -> Self {
        Self::new([x, y, z], [1.0, 1.0, 1.0], color, material)
    }

    pub fn decor(position: [f32; 3], size: [f32; 3], color: [u8; 3], material: Material) -> Self {
        Self {
            collidable: false,
            ..Self::new(position, size, color, material)
        }
    }

    pub fn center(&self) -> Vec3 {
        Vec3::from_slice(&self.position)
    }

    pub fn half_extents(&self) -> Vec3 {
        Vec3::from_slice(&self.size) * 0.5
    }
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct WorldMap {
    pub id: String,
    pub name: String,
    pub spawn: [f32; 3],
    /// Cor do "céu" (fundo) do mapa.
    pub sky_color: [u8; 3],
    pub blocks: Vec<Block>,
}

impl WorldMap {
    /// Constrói o mapa de teste `baseplate`.
    ///
    /// É uma homenagem à clássica *Baseplate* do Roblox 2008: gramado verde,
    /// alguns blocos, rampas improvisadas em degraus, árvores feitas de cilindros
    /// (aproximados por caixas), um ring/spawn e uma casa simples.
    pub fn test_map() -> Self {
        use Material::*;

        let grass: [u8; 3] = [104, 160, 60];
        let dark_grass: [u8; 3] = [78, 128, 45];
        let brick: [u8; 3] = [150, 70, 55];
        let wood: [u8; 3] = [124, 86, 48];
        let stone: [u8; 3] = [130, 130, 140];
        let trunk: [u8; 3] = [92, 64, 38];
        let leaves: [u8; 3] = [58, 120, 52];
        let red: [u8; 3] = [200, 60, 50];
        let yellow: [u8; 3] = [240, 200, 70];
        let blue: [u8; 3] = [60, 110, 200];
        let neon: [u8; 3] = [90, 230, 255];
        let gray: [u8; 3] = [90, 92, 100];
        let white: [u8; 3] = [235, 235, 235];

        let mut blocks = Vec::new();

        // --- Baseplate (80x80, topo em y = 0) ---
        blocks.push(Block::new(
            [0.0, -0.5, 0.0],
            [80.0, 1.0, 80.0],
            grass,
            Grass,
        ));

        // --- Caminho central até o spawn ---
        for i in 0..10 {
            let z = -12.0 + i as f32 * 1.5;
            blocks.push(Block::decor([0.0, 0.02, z], [3.0, 0.04, 1.4], stone, Stone));
        }

        // --- Escada / rampa em degraus (algo bem "2008") ---
        for i in 0..6 {
            let i = i as f32;
            blocks.push(Block::new(
                [-14.0, 0.25 + i * 0.5, -6.0 + i * 1.0],
                [6.0, 0.5 + i, 1.0],
                brick,
                Brick,
            ));
        }
        // Plataforma no topo da rampa
        blocks.push(Block::new([-14.0, 3.5, 1.5], [6.0, 1.0, 4.0], stone, Stone));

        // --- Pilha de "dominoes" coloridos ---
        blocks.push(Block::new([9.0, 1.0, -8.0], [1.0, 2.0, 1.0], red, Plastic));
        blocks.push(Block::new(
            [11.0, 2.0, -9.5],
            [1.0, 4.0, 1.0],
            yellow,
            Plastic,
        ));
        blocks.push(Block::new(
            [13.0, 1.5, -11.0],
            [1.0, 3.0, 1.0],
            blue,
            Plastic,
        ));

        // --- Casinha: paredes + teto + porta (abertura) ---
        let house_x = 16.0;
        let house_z = 10.0;
        // Piso levemente elevado
        blocks.push(Block::new(
            [house_x, 0.1, house_z],
            [10.0, 0.2, 10.0],
            wood,
            Wood,
        ));
        // Parede fundo
        blocks.push(Block::new(
            [house_x, 2.0, house_z + 5.0],
            [10.0, 4.0, 0.5],
            brick,
            Brick,
        ));
        // Paredes laterais
        blocks.push(Block::new(
            [house_x - 5.0, 2.0, house_z],
            [0.5, 4.0, 10.0],
            brick,
            Brick,
        ));
        blocks.push(Block::new(
            [house_x + 5.0, 2.0, house_z],
            [0.5, 4.0, 10.0],
            brick,
            Brick,
        ));
        // Frente com porta (dois lados + batente)
        blocks.push(Block::new(
            [house_x - 3.25, 2.0, house_z - 5.0],
            [3.5, 4.0, 0.5],
            brick,
            Brick,
        ));
        blocks.push(Block::new(
            [house_x + 3.25, 2.0, house_z - 5.0],
            [3.5, 4.0, 0.5],
            brick,
            Brick,
        ));
        blocks.push(Block::new(
            [house_x, 3.5, house_z - 5.0],
            [3.0, 1.0, 0.5],
            brick,
            Brick,
        ));
        // Teto
        blocks.push(Block::new(
            [house_x, 4.25, house_z],
            [10.5, 0.5, 10.5],
            gray,
            Stone,
        ));
        // Janela neon
        blocks.push(Block::decor(
            [house_x + 5.1, 2.4, house_z - 2.0],
            [0.1, 1.4, 1.4],
            neon,
            Neon,
        ));

        // --- Árvores (tronco + copa) ---
        for (x, z) in [(-20.0, 12.0), (-24.0, 4.0), (22.0, -16.0), (-10.0, -20.0)] {
            blocks.push(Block::new([x, 1.5, z], [1.0, 3.0, 1.0], trunk, Wood));
            blocks.push(Block::new([x, 4.0, z], [3.0, 2.0, 3.0], leaves, Grass));
            blocks.push(Block::new([x, 5.25, z], [2.0, 0.5, 2.0], leaves, Grass));
        }

        // --- Spawn pad: plataforma xadrez elevada ---
        for dx in -1..=1 {
            for dz in -1..=1 {
                let color = if (dx + dz) % 2 == 0 { white } else { red };
                blocks.push(Block::decor(
                    [dx as f32 * 2.0, 0.06, dz as f32 * 2.0],
                    [2.0, 0.12, 2.0],
                    color,
                    Plastic,
                ));
            }
        }

        // --- Postes de luz neon em volta ---
        for (x, z) in [(-8.0, -8.0), (8.0, -8.0), (-8.0, 8.0), (8.0, 8.0)] {
            blocks.push(Block::new([x, 1.5, z], [0.4, 3.0, 0.4], gray, Stone));
            blocks.push(Block::decor([x, 3.3, z], [0.8, 0.6, 0.8], neon, Neon));
        }

        // --- Muros-limite (fazem o mapa parecer um "estúdio") ---
        blocks.push(Block::new(
            [0.0, 1.0, -40.0],
            [80.0, 2.0, 0.6],
            dark_grass,
            Grass,
        ));
        blocks.push(Block::new(
            [0.0, 1.0, 40.0],
            [80.0, 2.0, 0.6],
            dark_grass,
            Grass,
        ));
        blocks.push(Block::new(
            [-40.0, 1.0, 0.0],
            [0.6, 2.0, 80.0],
            dark_grass,
            Grass,
        ));
        blocks.push(Block::new(
            [40.0, 1.0, 0.0],
            [0.6, 2.0, 80.0],
            dark_grass,
            Grass,
        ));

        Self {
            id: "baseplate".to_string(),
            name: "Baseplate Clássica".to_string(),
            spawn: [0.0, 0.2, 4.0],
            sky_color: [150, 200, 240],
            blocks,
        }
    }
}
