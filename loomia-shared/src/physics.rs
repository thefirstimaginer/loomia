//! Física do personagem — simples, determinística e **igual no cliente e no servidor**.
//!
//! O servidor é autoritativo: ele roda [`move_character`] com os inputs recebidos.
//! O cliente roda **exatamente a mesma função** com os seus próprios inputs para
//! prever o movimento (sem atraso perceptível) e depois suaviza a correção quando
//! o estado autoritativo chega — técnica de *client-side prediction*.
//!
//! Modelo estilo Roblox 2008: o personagem é uma caixa (AABB) que anda, pula,
//! cai com gravidade e colide com os blocos do mapa.

use crate::vec::Vec3;
use crate::world::Block;

pub const GRAVITY: f32 = -58.0;
pub const WALK_SPEED: f32 = 12.5;
pub const JUMP_VELOCITY: f32 = 21.0;
/// Aceleração no chão (o suficiente para sentir responsivo mas com inércia).
pub const GROUND_ACCEL: f32 = 90.0;
/// Aceleração no ar (controle aéreo reduzido).
pub const AIR_ACCEL: f32 = 22.0;
/// Atrito aplicado quando não há direção de movimento no chão.
pub const GROUND_FRICTION: f32 = 60.0;
pub const MAX_FALL_SPEED: f32 = -90.0;
/// Dimensões da "hitbox" do personagem (só é usada a parte X/Z como raio).
pub const CHARACTER_RADIUS: f32 = 0.55;
pub const CHARACTER_HEIGHT: f32 = 3.0;

/// Inputs de um tique (1/30 s) vindos do cliente.
#[derive(serde::Serialize, serde::Deserialize, Clone, Copy, Debug, Default, PartialEq)]
pub struct InputState {
    /// Número sequencial do tique gerado pelo cliente (base para reconciliação).
    pub seq: u32,
    /// Movimento desejado: X = direita/esquerda, Y = frente/trás ([-1, 1]).
    pub move_x: f32,
    pub move_z: f32,
    pub jump: bool,
}

impl InputState {
    /// Normaliza os eixos para que andar na diagonal não seja mais rápido.
    pub fn axes(&self) -> (f32, f32) {
        let (x, z) = (self.move_x.clamp(-1.0, 1.0), self.move_z.clamp(-1.0, 1.0));
        let len = (x * x + z * z).sqrt();
        if len > 1.0 {
            (x / len, z / len)
        } else {
            (x, z)
        }
    }
}

/// Estado físico do personagem. `position` é o ponto **entre os pés**.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CharacterState {
    pub position: Vec3,
    pub velocity: Vec3,
    pub yaw: f32,
    pub on_ground: bool,
}

impl CharacterState {
    pub fn new(position: Vec3, yaw: f32) -> Self {
        Self {
            position,
            velocity: Vec3::ZERO,
            yaw,
            on_ground: false,
        }
    }

    /// Caixa (min, max) ocupada pelo personagem na posição atual.
    pub fn aabb(&self) -> (Vec3, Vec3) {
        let p = self.position;
        (
            Vec3::new(p.x - CHARACTER_RADIUS, p.y, p.z - CHARACTER_RADIUS),
            Vec3::new(
                p.x + CHARACTER_RADIUS,
                p.y + CHARACTER_HEIGHT,
                p.z + CHARACTER_RADIUS,
            ),
        )
    }
}

#[inline]
fn aabb_overlap(a_min: Vec3, a_max: Vec3, b_min: Vec3, b_max: Vec3) -> bool {
    a_min.x < b_max.x
        && a_max.x > b_min.x
        && a_min.y < b_max.y
        && a_max.y > b_min.y
        && a_min.z < b_max.z
        && a_max.z > b_min.z
}

#[inline]
fn block_bounds(b: &Block) -> (Vec3, Vec3) {
    let c = b.center();
    let h = b.half_extents();
    (c - h, c + h)
}

/// Faixa vertical do mapa (mais alto bloco até o mais baixo), usada para otimizar a colisão.
pub fn world_bounds(blocks: &[Block]) -> (f32, f32) {
    let mut min = f32::INFINITY;
    let mut max = f32::NEG_INFINITY;
    for b in blocks {
        let c = b.center().y;
        let h = b.half_extents().y;
        min = min.min(c - h);
        max = max.max(c + h);
    }
    if blocks.is_empty() {
        (0.0, 0.0)
    } else {
        (min, max)
    }
}

/// Testa se a caixa do personagem em `position` intersecta algum bloco sólido.
pub fn collides(blocks: &[Block], position: Vec3) -> bool {
    let (min, max) = CharacterState {
        position,
        velocity: Vec3::ZERO,
        yaw: 0.0,
        on_ground: false,
    }
    .aabb();
    // Os pés podem encostar exatamente no topo de um bloco sem colidir.
    let min = Vec3::new(min.x, min.y + 0.001, min.z);
    blocks.iter().any(|b| {
        if !b.collidable {
            return false;
        }
        let (bmin, bmax) = block_bounds(b);
        aabb_overlap(min, max, bmin, bmax)
    })
}

/// Verifica se há chão logo abaixo dos pés (até `probe` unidades).
pub fn ground_below(blocks: &[Block], position: Vec3, probe: f32) -> bool {
    let (min, max) = CharacterState {
        position,
        velocity: Vec3::ZERO,
        yaw: 0.0,
        on_ground: false,
    }
    .aabb();
    let feet_min = Vec3::new(min.x + 0.01, min.y - probe, min.z + 0.01);
    let feet_max = Vec3::new(max.x - 0.01, min.y + 0.02, max.z - 0.01);
    blocks.iter().any(|b| {
        if !b.collidable {
            return false;
        }
        let (bmin, bmax) = block_bounds(b);
        aabb_overlap(feet_min, feet_max, bmin, bmax)
    })
}

/// Move o personagem um tique de `dt` segundos aplicando o input.
///
/// Devolve o novo estado. A colisão usa *sweep* por eixo (X, Z e depois Y) para se
/// comportar bem em cantos e ao subir em blocos de 1 estudinho de altura.
pub fn move_character(
    state: CharacterState,
    input: InputState,
    blocks: &[Block],
    dt: f32,
) -> CharacterState {
    let mut next = state;

    // --- Velocidade horizontal desejada ---
    let (ax, az) = input.axes();
    let wish = Vec3::new(ax, 0.0, az) * WALK_SPEED;
    let accel = if state.on_ground {
        GROUND_ACCEL
    } else {
        AIR_ACCEL
    };
    let diff = wish - next.velocity.horizontal();
    let diff_len = diff.length_horizontal();
    if diff_len > 1e-4 {
        let step = (accel * dt).min(diff_len);
        let dir = diff.normalized_horizontal();
        next.velocity.x += dir.x * step;
        next.velocity.z += dir.z * step;
    } else if state.on_ground && ax.abs() < 1e-4 && az.abs() < 1e-4 {
        // Atrito: para de deslizar quando não há direção.
        let speed = next.velocity.length_horizontal();
        let drop = (GROUND_FRICTION * dt).min(speed);
        let dir = next.velocity.normalized_horizontal();
        next.velocity.x -= dir.x * drop;
        next.velocity.z -= dir.z * drop;
    }

    // --- Gravidade ---
    if !state.on_ground {
        next.velocity.y = (next.velocity.y + GRAVITY * dt).max(MAX_FALL_SPEED);
    }

    // --- Pulo ---
    if input.jump && state.on_ground {
        next.velocity.y = JUMP_VELOCITY;
        next.on_ground = false;
    }

    // --- Eixo X ---
    let dx = next.velocity.x * dt;
    if dx != 0.0 {
        let mut candidate = next.position;
        candidate.x += dx;
        if collides(blocks, candidate) {
            // Degrau automático: tenta subir até 0.6 para não travar em quinas.
            let mut stepped = candidate;
            stepped.y += 0.6;
            if state.on_ground && !collides(blocks, stepped) {
                candidate = stepped;
            } else {
                candidate.x = next.position.x;
                next.velocity.x = 0.0;
            }
        }
        next.position = candidate;
    }

    // --- Eixo Z ---
    let dz = next.velocity.z * dt;
    if dz != 0.0 {
        let mut candidate = next.position;
        candidate.z += dz;
        if collides(blocks, candidate) {
            let mut stepped = candidate;
            stepped.y += 0.6;
            if state.on_ground && !collides(blocks, stepped) {
                candidate = stepped;
            } else {
                candidate.z = next.position.z;
                next.velocity.z = 0.0;
            }
        }
        next.position = candidate;
    }

    // --- Eixo Y ---
    let dy = next.velocity.y * dt;
    if dy != 0.0 {
        let mut candidate = next.position;
        candidate.y += dy;
        // Tolerância de 1 cm para "pousar" suavemente sobre o chão.
        if collides(blocks, candidate) {
            if dy < 0.0 {
                // Desce até encostar: aproximação por passos pequenos.
                let mut landed = next.position;
                let steps = 10;
                for i in 0..steps {
                    let probe = Vec3::new(
                        landed.x,
                        landed.y + (dy / steps as f32) * (i + 1) as f32,
                        landed.z,
                    );
                    if collides(blocks, probe) {
                        break;
                    }
                    landed = probe;
                }
                next.position = landed;
                next.on_ground = true;
            }
            next.velocity.y = 0.0;
        } else {
            next.position = candidate;
            // Detecta chão logo abaixo ao descer.
            next.on_ground = dy < 0.0 && ground_below(blocks, candidate, 0.05);
        }
    } else {
        next.on_ground = state.on_ground && ground_below(blocks, next.position, 0.06);
    }

    // Se algo empurrou o personagem para dentro de um bloco (ex.: spawn ruim), libera.
    if collides(blocks, next.position) {
        next.on_ground = false;
    }

    // --- Yaw: o personagem olha para a direção em que anda ---
    if let Some(yaw) = facing_yaw_from_movement(ax, az, next.yaw) {
        next.yaw = yaw;
    }

    next
}

/// Yaw (radianos) para onde o personagem deve olhar, baseado na câmera e no input.
/// `camera_yaw` é a rotação horizontal da câmera (0 = olhando para -Z).
fn facing_yaw_from_movement(ax: f32, az: f32, _current: f32) -> Option<f32> {
    if ax.abs() < 1e-3 && az.abs() < 1e-3 {
        return None;
    }
    // O cliente envia os eixos já no espaço do mundo (relativo à câmera),
    // então o yaw é simplesmente atan2(x, z).
    Some(ax.atan2(az))
}

/// Direção vertical corrigida para um personagem que caiu do mapa (respawn).
pub fn fell_out_of_world(state: &CharacterState, blocks: &[Block]) -> bool {
    let (min_y, _) = world_bounds(blocks);
    state.position.y < min_y - 30.0
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::world::Material;

    fn floor() -> Vec<Block> {
        vec![Block::new(
            [0.0, -0.5, 0.0],
            [20.0, 1.0, 20.0],
            [0, 0, 0],
            Material::Plastic,
        )]
    }

    #[test]
    fn character_falls_and_lands() {
        let blocks = floor();
        let mut state = CharacterState::new(Vec3::new(0.0, 5.0, 0.0), 0.0);
        for _ in 0..120 {
            state = move_character(state, InputState::default(), &blocks, 1.0 / 30.0);
        }
        assert!(
            state.on_ground,
            "deveria estar no chão; y={}",
            state.position.y
        );
        assert!(
            (state.position.y - 0.0).abs() < 0.15,
            "y = {}",
            state.position.y
        );
    }

    #[test]
    fn walking_moves_horizontally() {
        let blocks = floor();
        let mut state = CharacterState::new(Vec3::new(0.0, 0.0, 0.0), 0.0);
        state.on_ground = true;
        for _ in 0..60 {
            state = move_character(
                state,
                InputState {
                    move_z: 1.0,
                    ..Default::default()
                },
                &blocks,
                1.0 / 30.0,
            );
        }
        assert!(state.position.z > 5.0, "z = {}", state.position.z);
    }

    #[test]
    fn wall_stops_movement() {
        let mut blocks = floor();
        blocks.push(Block::new(
            [0.0, 1.5, 4.0],
            [4.0, 3.0, 0.5],
            [0, 0, 0],
            Material::Brick,
        ));
        let mut state = CharacterState::new(Vec3::new(0.0, 0.0, 0.0), 0.0);
        state.on_ground = true;
        for _ in 0..180 {
            state = move_character(
                state,
                InputState {
                    move_z: 1.0,
                    ..Default::default()
                },
                &blocks,
                1.0 / 30.0,
            );
        }
        assert!(
            state.position.z < 3.8,
            "atravessou a parede: z = {}",
            state.position.z
        );
    }

    #[test]
    fn can_jump() {
        let blocks = floor();
        let mut state = CharacterState::new(Vec3::new(0.0, 0.0, 0.0), 0.0);
        state.on_ground = true;
        state = move_character(
            state,
            InputState {
                jump: true,
                ..Default::default()
            },
            &blocks,
            1.0 / 30.0,
        );
        assert!(state.velocity.y > 0.0);
        let mut peak = 0.0_f32;
        for _ in 0..60 {
            state = move_character(state, InputState::default(), &blocks, 1.0 / 30.0);
            peak = peak.max(state.position.y);
        }
        assert!(peak > 2.5, "pulo baixo: {peak}");
    }
}
