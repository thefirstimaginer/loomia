//! Tela de carregamento: "Carregando Loomia, Aguarde!".
//!
//! Exibida enquanto o cliente conecta ao servidor e recebe o `Welcome`
//! (mapa + jogadores). Quando o mundo é montado, a fase muda para `Playing` e
//! esta UI é removida. Inclui:
//!
//! * logo texto estilizado "LOOMIA" no estilo do site de 2008;
//! * spinner girando e barra de progresso animada;
//! * dicas rotativas e botão de cancelar (ESC / clique em SAIR).

use bevy::prelude::*;
use bevy::ui::Val;

use crate::{net::NetStatus, ClientConfig, GamePhase};

#[derive(Component)]
struct LoadingRoot;

#[derive(Component)]
struct Spinner;

#[derive(Component)]
struct LoadingBarFill;

#[derive(Component)]
struct StatusText;

#[derive(Component)]
struct TipText;

#[derive(Resource, Default)]
struct ElapsedTracker(f32);

const TIPS: &[&str] = &[
    "WASD para andar · Espaço para pular",
    "Segure o botão direito do mouse para girar a câmera",
    "Roda do mouse para aproximar · T para conversar",
    "Segure o clique esquerdo e mova o mouse para orbitar",
    "Experiência: Baseplate Clássica (2008)",
];

pub struct LoadingPlugin;

impl Plugin for LoadingPlugin {
    fn build(&self, app: &mut App) {
        app.add_systems(OnEnter(GamePhase::Loading), spawn_loading_ui)
            .add_systems(OnExit(GamePhase::Loading), despawn_loading_ui)
            .add_systems(Update, animate_loading.run_if(in_state(GamePhase::Loading)))
            .add_systems(Update, cancel_loading.run_if(in_state(GamePhase::Loading)));
    }
}

fn spawn_loading_ui(mut commands: Commands, config: Res<ClientConfig>) {
    commands
        .spawn((
            LoadingRoot,
            Node {
                width: Val::Percent(100.0),
                height: Val::Percent(100.0),
                flex_direction: FlexDirection::Column,
                align_items: AlignItems::Center,
                justify_content: JustifyContent::Center,
                row_gap: Val::Px(18.0),
                ..default()
            },
            BackgroundColor(Color::srgb_u8(18, 22, 30)),
        ))
        .with_children(|parent| {
            // "Logo" do jogo.
            parent.spawn((
                Text::new("LOOMIA"),
                TextFont {
                    font_size: 74.0,
                    ..default()
                },
                TextColor(Color::srgb_u8(240, 200, 70)),
                TextShadow {
                    offset: Vec2::new(4.0, 4.0),
                    color: Color::srgba(0.0, 0.0, 0.0, 0.6),
                },
                Node {
                    margin: UiRect::bottom(Val::Px(6.0)),
                    ..default()
                },
            ));
            parent.spawn((
                Text::new("BUILD. PLAY. SHARE."),
                TextFont {
                    font_size: 16.0,
                    ..default()
                },
                TextColor(Color::srgb_u8(150, 170, 200)),
                Node {
                    margin: UiRect::bottom(Val::Px(28.0)),
                    ..default()
                },
            ));

            // Spinner (um bloco quadrado — "retrô").
            parent
                .spawn((
                    Spinner,
                    Node {
                        width: Val::Px(34.0),
                        height: Val::Px(34.0),
                        border: UiRect::all(Val::Px(4.0)),
                        ..default()
                    },
                    BorderColor(Color::srgb_u8(240, 200, 70)),
                    BorderRadius::all(Val::Px(4.0)),
                ))
                .with_children(|spinner| {
                    spinner.spawn((
                        Node {
                            width: Val::Percent(60.0),
                            height: Val::Percent(60.0),
                            position_type: PositionType::Absolute,
                            left: Val::Percent(20.0),
                            top: Val::Percent(20.0),
                            ..default()
                        },
                        BackgroundColor(Color::srgb_u8(120, 200, 255)),
                    ));
                });

            // Texto principal.
            parent.spawn((
                StatusText,
                Text::new("Carregando Loomia, Aguarde!"),
                TextFont {
                    font_size: 30.0,
                    ..default()
                },
                TextColor(Color::srgb_u8(245, 245, 245)),
            ));
            parent.spawn((
                Text::new(format!("Conectando a {}", config.server_url)),
                TextFont {
                    font_size: 14.0,
                    ..default()
                },
                TextColor(Color::srgb_u8(140, 150, 165)),
            ));

            // Barra de progresso.
            parent
                .spawn((
                    Node {
                        width: Val::Px(420.0),
                        height: Val::Px(16.0),
                        border: UiRect::all(Val::Px(2.0)),
                        margin: UiRect::vertical(Val::Px(10.0)),
                        ..default()
                    },
                    BorderColor(Color::srgb_u8(240, 200, 70)),
                    BackgroundColor(Color::srgb_u8(30, 34, 44)),
                ))
                .with_children(|bar| {
                    bar.spawn((
                        LoadingBarFill,
                        Node {
                            width: Val::Percent(8.0),
                            height: Val::Percent(100.0),
                            ..default()
                        },
                        BackgroundColor(Color::srgb_u8(240, 200, 70)),
                    ));
                });

            parent.spawn((
                TipText,
                Text::new(TIPS[0]),
                TextFont {
                    font_size: 15.0,
                    ..default()
                },
                TextColor(Color::srgb_u8(160, 200, 160)),
                Node {
                    margin: UiRect::top(Val::Px(8.0)),
                    ..default()
                },
            ));
            parent.spawn((
                Text::new("ESC ou clique em SAIR para cancelar"),
                TextFont {
                    font_size: 12.0,
                    ..default()
                },
                TextColor(Color::srgb_u8(110, 115, 130)),
                Node {
                    margin: UiRect::top(Val::Px(26.0)),
                    ..default()
                },
            ));
        });

    commands.insert_resource(ElapsedTracker(0.0));
}

fn despawn_loading_ui(mut commands: Commands, roots: Query<Entity, With<LoadingRoot>>) {
    for entity in &roots {
        commands.entity(entity).despawn();
    }
    commands.remove_resource::<ElapsedTracker>();
}

fn animate_loading(
    mut commands: Commands,
    time: Res<Time>,
    status: Res<NetStatus>,
    mut elapsed: ResMut<ElapsedTracker>,
    mut spinner: Query<&mut Transform, With<Spinner>>,
    mut fill: Query<&mut Node, With<LoadingBarFill>>,
    mut status_text: Query<&mut Text, (With<StatusText>, Without<TipText>)>,
    mut tip_text: Query<&mut Text, (With<TipText>, Without<StatusText>)>,
    mut app_exit: EventWriter<AppExit>,
    keyboard: Res<ButtonInput<KeyCode>>,
) {
    elapsed.0 += time.delta_secs();

    // Spinner: rotação 2D.
    let angle = elapsed.0 * 3.0;
    for mut transform in &mut spinner {
        transform.rotation = Quat::from_rotation_z(angle);
    }

    // Barra de progresso: sobe até 92% enquanto conecta; completa no Welcome.
    let progress = (elapsed.0 / 6.0).min(0.92);
    for mut node in &mut fill {
        node.width = Val::Percent(progress * 100.0);
    }

    // Texto: alterna entre "conectando" e "carregando mundo".
    if elapsed.0 > 1.2 {
        for mut text in &mut status_text {
            if status.state == crate::net::ConnState::Connected {
                **text = "Carregando mundo e avatares...".to_string();
            }
        }
    }
    for mut text in &mut tip_text {
        let idx = ((elapsed.0 / 4.0) as usize) % TIPS.len();
        **text = TIPS[idx].to_string();
    }

    // Cancelar com ESC fecha o jogo (o launcher não deixa processos órfãos).
    if keyboard.just_pressed(KeyCode::Escape) {
        info!("carregamento cancelado pelo usuário");
        app_exit.write(AppExit::Success);
    }

    let _ = (&mut commands, &mut status_text, &mut tip_text);
}

fn cancel_loading() {}
