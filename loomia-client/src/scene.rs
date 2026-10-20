//! Cena: câmera em terceira pessoa, luzes e céu.

use bevy::prelude::*;

use crate::player::CameraOrbit;

pub struct ScenePlugin;

impl Plugin for ScenePlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(Startup, spawn_scene);
    }
}

fn spawn_scene(mut commands: Commands, mut clear: ResMut<ClearColor>) {
    *clear = ClearColor(Color::srgb_u8(150, 200, 240));

    // Luz principal do "sol" com sombras (estilo 2008, mas com sombras suaves).
    commands.spawn((
        DirectionalLight {
            illuminance: 11_000.0,
            shadows_enabled: true,
            ..default()
        },
        Transform::from_xyz(30.0, 60.0, 25.0).looking_at(Vec3::ZERO, Vec3::Y),
    ));

    // Luz ambiente azul-clara simulando o céu.
    commands.insert_resource(AmbientLight {
        color: Color::srgb(0.75, 0.82, 0.95),
        brightness: 420.0,
        affects_lightmapped_meshes: false,
    });

    // Câmera de terceira pessoa orbitando o personagem local.
    let orbit = CameraOrbit {
        yaw: 0.0,
        pitch: -0.32,
        distance: 9.5,
    };
    let focus = Vec3::new(0.0, 2.0, 4.0);
    let translation = focus + orbit.offset();
    commands.spawn((
        Camera3d::default(),
        Transform::from_translation(translation).looking_at(focus, Vec3::Y),
        orbit,
        Name::new("main-camera"),
    ));
}
