//! Renderização do mapa: instancia um cubo por bloco do `Welcome`.
//!
//! Cada bloco é uma entidade com um cubo de 1x1x1 e `Transform` escalado para o
//! tamanho do bloco. Materiais são reaproveitados por (cor, material) para
//! reduzir o número de *draw calls* sem complicar o código.

use std::collections::HashMap;

use bevy::prelude::*;
use loomia_shared::world::{Block, Material, WorldMap};

use crate::player::WelcomeData;

#[derive(Component)]
pub struct MapBlock;

#[derive(Resource, Default)]
pub struct MapHandle {
    pub map: Option<WorldMap>,
}

pub struct WorldRenderPlugin;

impl Plugin for WorldRenderPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<MapHandle>()
            .add_systems(Startup, spawn_fallback_ground)
            .add_systems(Update, spawn_map_from_welcome);
    }
}

/// Chão de emergência, visível na tela de carregamento (antes do mapa chegar),
/// para que o mundo não pareça um vazio preto.
fn spawn_fallback_ground(
    mut commands: Commands,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    mut clear: ResMut<ClearColor>,
) {
    *clear = ClearColor(Color::srgb_u8(150, 200, 240));
    commands.spawn((
        Mesh3d(meshes.add(Cuboid::new(80.0, 1.0, 80.0))),
        MeshMaterial3d(materials.add(StandardMaterial {
            base_color: Color::srgb_u8(90, 120, 80),
            perceptual_roughness: 0.95,
            ..default()
        })),
        Transform::from_xyz(0.0, -0.5, 0.0),
        Name::new("fallback-ground"),
    ));
}

fn spawn_map_from_welcome(
    mut commands: Commands,
    mut map_handle: ResMut<MapHandle>,
    welcome: Option<Res<WelcomeData>>,
    existing: Query<Entity, With<MapBlock>>,
    mut meshes: ResMut<Assets<Mesh>>,
    mut materials: ResMut<Assets<StandardMaterial>>,
    mut clear: ResMut<ClearColor>,
) {
    let Some(welcome) = welcome else { return };
    if map_handle.map.is_some() {
        return;
    }

    // Limpa o chão provisório.
    for entity in &existing {
        commands.entity(entity).despawn();
    }

    let map = &welcome.map;
    *clear = ClearColor(Color::srgb_u8(
        map.sky_color[0],
        map.sky_color[1],
        map.sky_color[2],
    ));
    info!("montando mapa '{}' ({} blocos)", map.name, map.blocks.len());

    let mesh = meshes.add(Cuboid::new(1.0, 1.0, 1.0));
    let mut material_cache: HashMap<(String, bool), Handle<StandardMaterial>> = HashMap::new();

    for block in &map.blocks {
        let key = (
            format!(
                "{}:{:?}:{}",
                rgb_hex(block.color),
                block.material,
                block.collidable
            ),
            block.collidable,
        );
        let material = material_cache
            .entry(key.clone())
            .or_insert_with(|| {
                let color = Color::srgb_u8(block.color[0], block.color[1], block.color[2]);
                let emissive_strength = block.material.emissive();
                let material = match block.material {
                    Material::Grass => StandardMaterial {
                        base_color: color,
                        perceptual_roughness: 1.0,
                        reflectance: 0.02,
                        ..default()
                    },
                    Material::Wood => StandardMaterial {
                        base_color: color,
                        perceptual_roughness: 0.85,
                        ..default()
                    },
                    Material::Brick => StandardMaterial {
                        base_color: color,
                        perceptual_roughness: 0.9,
                        ..default()
                    },
                    Material::Stone => StandardMaterial {
                        base_color: color,
                        perceptual_roughness: 0.7,
                        ..default()
                    },
                    Material::Plastic => StandardMaterial {
                        base_color: color,
                        perceptual_roughness: 0.35,
                        reflectance: 0.15,
                        ..default()
                    },
                    Material::Neon => StandardMaterial {
                        base_color: color,
                        emissive: (color.to_linear() * emissive_strength).into(),
                        ..default()
                    },
                };
                materials.add(material)
            })
            .clone();

        spawn_block(&mut commands, mesh.clone(), material, block);
    }

    map_handle.map = Some(map.clone());
}

fn rgb_hex(c: [u8; 3]) -> String {
    format!("{:02x}{:02x}{:02x}", c[0], c[1], c[2])
}

fn spawn_block(
    commands: &mut Commands,
    mesh: Handle<Mesh>,
    material: Handle<StandardMaterial>,
    block: &Block,
) {
    commands.spawn((
        MapBlock,
        Mesh3d(mesh),
        MeshMaterial3d(material),
        Transform::from_xyz(block.position[0], block.position[1], block.position[2])
            .with_scale(Vec3::new(block.size[0], block.size[1], block.size[2])),
    ));
}
