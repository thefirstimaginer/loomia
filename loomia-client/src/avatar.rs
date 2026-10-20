//! Avatares dos jogadores no estilo 2008.
//!
//! Um avatar é montado com blocos simples (como no Roblox clássico):
//!
//! ```text
//!          [ chapéu ]        <- opcional (boné, cartola, hélice, coroa)
//!          [ cabeça ] 1.25²
//!  [ braço ][ torso ][ braço ]   torso 1.0 x 1.4 x 0.6
//!          [ pernas DS ]          pernas 0.5 x 1.0
//! ```
//!
//! A origem do avatar fica entre os pés (igual a `CharacterState::position`), então
//! o `Transform` pode ser sincronizado direto com a física. Braços e pernas
//! balançam conforme a velocidade horizontal; o avatar inteiro gira com o `yaw`.

use std::collections::{HashMap, HashSet};

use bevy::prelude::*;
use loomia_shared::{
    player::{Appearance, Hat},
    protocol::PlayerStateWire,
    vec::Vec3 as SVec3,
};

use crate::player::LocalPlayer;

#[derive(Component, Debug)]
pub struct Avatar {
    pub player_id: u32,
    /// Posição suavizada (interpolação entre snapshots).
    pub display_pos: SVec3,
    /// Tempo de balanço próprio (dessincroniza os avatares).
    pub walk_clock: f32,
}

/// Braço ou perna que balança ao andar. `phase` em ±1 define o sentido.
#[derive(Component)]
pub struct Limb {
    pub phase: f32,
    pub is_arm: bool,
}

/// Parte do avatar que gira (hélice do chapéu).
#[derive(Component)]
pub struct Spinner;

#[derive(Resource, Default)]
pub struct AvatarCache {
    pub built: HashMap<u32, Entity>,
    /// Últimos estados recebidos do servidor (por jogador).
    pub latest: HashMap<u32, PlayerStateWire>,
    /// Velocidade horizontal estimada (para animar as pernas).
    pub speed: HashMap<u32, f32>,
    /// Posição no snapshot anterior (para estimar velocidade remota).
    pub previous: HashMap<u32, [f32; 3]>,
}

pub struct AvatarPlugin;

impl Plugin for AvatarPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<AvatarCache>()
            .init_resource::<AvatarAssets>()
            .add_systems(Update, (reconcile_snapshots, spawn_new_avatars, despawn_missing_avatars, animate_avatars).chain());
    }
}

/// Malha compartilhada por todas as partes dos avatares.
#[derive(Resource)]
pub struct AvatarAssets {
    pub cube: Handle<Mesh>,
}

impl FromWorld for AvatarAssets {
    fn from_world(world: &mut World) -> Self {
        let mut meshes = world.resource_mut::<Assets<Mesh>>();
        Self { cube: meshes.add(Cuboid::new(1.0, 1.0, 1.0)) }
    }
}

/// Copia o último snapshot para o cache de avatares.
fn reconcile_snapshots(mut player: ResMut<LocalPlayer>, mut cache: ResMut<AvatarCache>) {
    let Some(snapshot) = player.latest_snapshot.take() else { return };
    let mut seen = HashSet::new();
    for state in snapshot {
        seen.insert(state.id);
        cache.latest.insert(state.id, state);
    }
    cache.latest.retain(|id, _| seen.contains(id));
}

fn spawn_new_avatars(
    mut commands: Commands,
    mut cache: ResMut<AvatarCache>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    assets: Res<AvatarAssets>,
    local: Res<LocalPlayer>,
) {
    let ids: Vec<u32> = cache.latest.keys().copied().collect();
    for id in ids {
        if cache.built.contains_key(&id) {
            continue;
        }
        let Some(state) = cache.latest.get(&id).cloned() else { continue };
        let is_local = local.player_id() == Some(id);
        let position = state.position;
        let appearance = state.appearance.clone();
        let entity = commands
            .spawn((
                Avatar {
                    player_id: id,
                    display_pos: SVec3::from_slice(&position),
                    walk_clock: id as f32 * 0.7,
                },
                Transform::from_xyz(position[0], position[1], position[2]),
                Visibility::default(),
                Name::new(format!("avatar-{}-{}", id, appearance.username)),
            ))
            .with_children(|parent| build_body(parent, &assets, &mut materials, &appearance))
            .id();
        cache.built.insert(id, entity);
        info!(
            "avatar de '{}' (id {}) criado{}",
            appearance.username,
            id,
            if is_local { " - jogador local" } else { "" }
        );
    }
}

fn despawn_missing_avatars(mut commands: Commands, mut cache: ResMut<AvatarCache>) {
    let missing: Vec<u32> = cache.built.keys().copied().filter(|id| !cache.latest.contains_key(id)).collect();
    for id in missing {
        if let Some(entity) = cache.built.remove(&id) {
            commands.entity(entity).despawn();
            info!("avatar {id} removido");
        }
        cache.speed.remove(&id);
        cache.previous.remove(&id);
    }
}

/// Interpola posição e anima membros/olhar usando ParamSet para evitar conflitos de query.
fn animate_avatars(
    time: Res<Time>,
    mut cache: ResMut<AvatarCache>,
    local: Res<LocalPlayer>,
    mut avatars: Query<(&mut Avatar, &mut Transform, &Children), Without<Limb>>,
    mut limb_query: Query<(&Limb, &mut Transform), (Without<Spinner>, Without<Avatar>)>,
    mut spinner_query: Query<&mut Transform, (With<Spinner>, Without<Avatar>, Without<Limb>)>,
) {
    let delta = time.delta_secs();
    let lerp_factor = (delta * 20.0).min(1.0);

    // 1. Atualiza velocidades remotas no cache
    let ids: Vec<u32> = cache.latest.keys().copied().collect();
    for id in ids {
        let Some(state) = cache.latest.get(&id) else { continue };
        let position = state.position;
        let speed = match cache.previous.get(&id) {
            Some(prev) => {
                let dx = position[0] - prev[0];
                let dz = position[2] - prev[2];
                (dx * dx + dz * dz).sqrt() * 30.0
            }
            None => 0.0,
        };
        let smoothed = cache.speed.get(&id).copied().unwrap_or(0.0) * 0.7 + speed * 0.3;
        cache.speed.insert(id, smoothed);
        cache.previous.insert(id, position);
    }

    // 2. Anima os avatares principais (Raiz)
    for (mut avatar, mut transform, children) in &mut avatars {
        let is_local = local.player_id() == Some(avatar.player_id);

        let target = if is_local {
            local.predicted.position
        } else {
            let Some(state) = cache.latest.get(&avatar.player_id) else { continue };
            SVec3::from_slice(&state.position)
        };

        if (avatar.display_pos - target).length_horizontal() > 10.0 || (avatar.display_pos.y - target.y).abs() > 6.0 {
            avatar.display_pos = target;
        } else {
            avatar.display_pos = avatar.display_pos.lerp(target, lerp_factor);
        }
        transform.translation = Vec3::new(avatar.display_pos.x, avatar.display_pos.y, avatar.display_pos.z);

        let yaw = if is_local {
            local.predicted.yaw
        } else {
            cache.latest.get(&avatar.player_id).map(|s| s.yaw).unwrap_or(0.0)
        };
        transform.rotation = Quat::from_rotation_y(yaw);

        let speed = if is_local {
            local.predicted.velocity.length_horizontal()
        } else {
            cache.speed.get(&avatar.player_id).copied().unwrap_or(0.0)
        };
        if speed > 0.5 {
            avatar.walk_clock += delta * (4.0 + speed * 0.55);
        }
        let swing = if speed > 0.5 { avatar.walk_clock.sin() * 0.65 } else { 0.0 };

        // Anima os membros filhos
        for child in children.iter() {
            if let Ok((limb, mut limb_transform)) = limb_query.get_mut(*child) {
                let base = if limb.is_arm { -swing } else { swing };
                limb_transform.rotation = Quat::from_rotation_x(base * limb.phase);
            }
        }

        // Anima os spinners (hélices) filhos
        for child in children.iter() {
            if let Ok(mut spinner_transform) = spinner_query.get_mut(*child) {
                spinner_transform.rotation = Quat::from_rotation_y(time.elapsed_secs() * 6.0);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Construção do corpo
// ---------------------------------------------------------------------------

fn build_body(
    parent: &mut ChildSpawnerCommands,
    assets: &AvatarAssets,
    materials: &mut Assets<StandardMaterial>,
    appearance: &Appearance,
) {
    let skin = Color::srgb_u8(appearance.body_color[0], appearance.body_color[1], appearance.body_color[2]);
    let shirt = Color::srgb_u8(appearance.shirt_color[0], appearance.shirt_color[1], appearance.shirt_color[2]);
    let pants = Color::srgb_u8(appearance.pants_color[0], appearance.pants_color[1], appearance.pants_color[2]);

    let plain = |materials: &mut Assets<StandardMaterial>, color: Color| {
        materials.add(StandardMaterial { base_color: color, perceptual_roughness: 0.75, ..default() })
    };

    // Pernas (y de 0 a 1).
    for (phase, x) in [(1.0, -0.25), (-1.0, 0.25)] {
        parent.spawn((
            Mesh3d(assets.cube.clone()),
            MeshMaterial3d(plain(materials, pants)),
            Transform::from_xyz(x, 0.5, 0.0).with_scale(Vec3::new(0.5, 1.0, 0.5)),
            Limb { phase, is_arm: false },
            Name::new("leg"),
        ));
    }

    // Torso (y de 1.0 a 2.4).
    parent.spawn((
        Mesh3d(assets.cube.clone()),
        MeshMaterial3d(plain(materials, shirt)),
        Transform::from_xyz(0.0, 1.7, 0.0).with_scale(Vec3::new(1.0, 1.4, 0.6)),
        Name::new("torso"),
    ));

    // Braços com pivô no ombro (y = 2.3).
    for (phase, x) in [(1.0, -0.72), (-1.0, 0.72)] {
        let sleeve = plain(materials, shirt);
        let hand = plain(materials, skin);
        parent
            .spawn((
                Transform::from_xyz(x, 2.3, 0.0),
                Visibility::default(),
                Limb { phase, is_arm: true },
                Name::new("shoulder"),
            ))
            .with_children(|shoulder| {
                shoulder.spawn((
                    Mesh3d(assets.cube.clone()),
                    MeshMaterial3d(sleeve),
                    Transform::from_xyz(0.0, -0.55, 0.0).with_scale(Vec3::new(0.45, 1.1, 0.45)),
                    Name::new("arm"),
                ));
                shoulder.spawn((
                    Mesh3d(assets.cube.clone()),
                    MeshMaterial3d(hand),
                    Transform::from_xyz(0.0, -1.18, 0.0).with_scale(Vec3::new(0.42, 0.24, 0.42)),
                    Name::new("hand"),
                ));
            });
    }

    // Cabeça (y de 2.4 a 3.6).
    parent.spawn((
        Mesh3d(assets.cube.clone()),
        MeshMaterial3d(plain(materials, skin)),
        Transform::from_xyz(0.0, 2.98, 0.0).with_scale(Vec3::splat(1.22)),
        Name::new("head"),
    ));

    // Olhos e sorriso (o avatar olha para -Z).
    let dark = plain(materials, Color::srgb_u8(20, 20, 25));
    for x in [-0.25, 0.25] {
        parent.spawn((
            Mesh3d(assets.cube.clone()),
            MeshMaterial3d(dark.clone()),
            Transform::from_xyz(x, 3.08, -0.62).with_scale(Vec3::new(0.17, 0.2, 0.04)),
            Name::new("eye"),
        ));
    }
    parent.spawn((
        Mesh3d(assets.cube.clone()),
        MeshMaterial3d(plain(materials, Color::srgb_u8(70, 35, 35))),
        Transform::from_xyz(0.0, 2.72, -0.62).with_scale(Vec3::new(0.4, 0.06, 0.04)),
        Name::new("mouth"),
    ));

    if let Some(hat) = appearance.hat {
        build_hat(parent, assets, materials, hat);
    }
}

fn build_hat(parent: &mut ChildSpawnerCommands, assets: &AvatarAssets, materials: &mut Assets<StandardMaterial>, hat: Hat) {
    let plain = |materials: &mut Assets<StandardMaterial>, color: Color| {
        materials.add(StandardMaterial { base_color: color, perceptual_roughness: 0.6, ..default() })
    };
    const HEAD_TOP: f32 = 3.59;

    match hat {
        Hat::Cap => {
            parent.spawn((
                Mesh3d(assets.cube.clone()),
                MeshMaterial3d(plain(materials, Color::srgb_u8(200, 45, 45))),
                Transform::from_xyz(0.0, HEAD_TOP + 0.14, 0.0).with_scale(Vec3::new(1.28, 0.36, 1.28)),
                Name::new("cap"),
            ));
            parent.spawn((
                Mesh3d(assets.cube.clone()),
                MeshMaterial3d(plain(materials, Color::srgb_u8(160, 35, 35))),
                Transform::from_xyz(0.0, HEAD_TOP - 0.02, -0.82).with_scale(Vec3::new(1.05, 0.09, 0.65)),
                Name::new("cap-brim"),
            ));
        }
        Hat::TopHat => {
            parent.spawn((
                Mesh3d(assets.cube.clone()),
                MeshMaterial3d(plain(materials, Color::srgb_u8(25, 25, 30))),
                Transform::from_xyz(0.0, HEAD_TOP + 0.03, 0.0).with_scale(Vec3::new(1.6, 0.12, 1.6)),
                Name::new("tophat-brim"),
            ));
            parent.spawn((
                Mesh3d(assets.cube.clone()),
                MeshMaterial3d(plain(materials, Color::srgb_u8(30, 30, 36))),
                Transform::from_xyz(0.0, HEAD_TOP + 0.7, 0.0).with_scale(Vec3::new(1.0, 1.2, 1.0)),
                Name::new("tophat"),
            ));
        }
        Hat::Propeller => {
            parent.spawn((
                Mesh3d(assets.cube.clone()),
                MeshMaterial3d(plain(materials, Color::srgb_u8(250, 200, 40))),
                Transform::from_xyz(0.0, HEAD_TOP + 0.12, 0.0).with_scale(Vec3::new(1.26, 0.3, 1.26)),
                Name::new("propeller-cap"),
            ));
            parent
                .spawn((
                    Transform::from_xyz(0.0, HEAD_TOP + 0.36, 0.0),
                    Visibility::default(),
                    Spinner,
                    Name::new("propeller"),
                ))
                .with_children(|spinner| {
                    for rotation in [0.0_f32, std::f32::consts::FRAC_PI_2] {
                        spinner.spawn((
                            Mesh3d(assets.cube.clone()),
                            MeshMaterial3d(plain(materials, Color::srgb_u8(220, 60, 60))),
                            Transform::from_rotation(Quat::from_rotation_y(rotation))
                                .with_scale(Vec3::new(1.5, 0.06, 0.22)),
                            Name::new("blade"),
                        ));
                    }
                });
        }
        Hat::Crown => {
            parent.spawn((
                Mesh3d(assets.cube.clone()),
                MeshMaterial3d(plain(materials, Color::srgb_u8(240, 195, 60))),
                Transform::from_xyz(0.0, HEAD_TOP + 0.12, 0.0).with_scale(Vec3::new(1.2, 0.26, 1.2)),
                Name::new("crown-band"),
            ));
            for (x, z) in [(-0.42, -0.42), (0.42, -0.42), (-0.42, 0.42), (0.42, 0.42)] {
                parent.spawn((
                    Mesh3d(assets.cube.clone()),
                    MeshMaterial3d(plain(materials, Color::srgb_u8(240, 195, 60))),
                    Transform::from_xyz(x, HEAD_TOP + 0.38, z).with_scale(Vec3::new(0.2, 0.3, 0.2)),
                    Name::new("crown-tip"),
                ));
            }
        }
    }
}
