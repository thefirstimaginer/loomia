//! HUD: chat clássico (canto superior esquerdo), placar de jogadores, latência e
//! dicas de controle.
//!
//! * `T` ou `Enter` abre o chat; `Enter` envia, `Esc` cancela.
//! * `F1` mostra/esconde a lista de jogadores e a dica de controles.
//!
//! O HUD é deliberadamente "quadradão" e amarelo-escuro, seguindo o estilo de
//! interface do Roblox de 2008.

use bevy::prelude::*;
use bevy::ui::Val;
use loomia_shared::protocol::{ClientMessage, PlayerStateWire, MAX_CHAT_LEN};

use crate::{
    avatar::AvatarCache,
    net::{ConnState, NetStatus, Outbound},
    player::LocalPlayer,
    ClientConfig, GamePhase,
};

/// Evento de mensagem de chat recebida (enviado pelo sistema de rede).
#[derive(Event, Debug)]
pub struct ChatLine {
    pub from: String,
    pub text: String,
}

/// Evento de atualização do placar (entrou/saiu).
#[derive(Event, Debug)]
pub enum ServerPlayers {
    Joined(PlayerStateWire),
    Left(u32),
}

#[derive(Resource, Default)]
pub struct ChatState {
    pub open: bool,
    pub input: String,
    pub lines: Vec<(String, String)>,
    pub max_lines: usize,
}

#[derive(Component)]
pub struct ChatHistoryText;

#[derive(Component)]
pub struct ChatInputField;

#[derive(Component)]
pub struct PlayerListText;

#[derive(Component)]
pub struct DiagnosticsText;

#[derive(Component)]
pub struct HelpText;

#[derive(Component)]
pub struct ChatPanel;

pub struct HudPlugin;

impl Plugin for HudPlugin {
    fn build(&self, app: &mut App) {
        app.init_resource::<ChatState>()
            .add_event::<ChatLine>()
            .add_event::<ServerPlayers>()
            .add_systems(OnEnter(GamePhase::Playing), spawn_hud)
            .add_systems(OnExit(GamePhase::Playing), despawn_hud)
            .add_systems(
                Update,
                (collect_chat_lines, toggle_and_type_chat, update_hud_texts).run_if(in_state(GamePhase::Playing)),
            );
    }
}

fn spawn_hud(mut commands: Commands, config: Res<ClientConfig>) {
    let mut chat = ChatState { max_lines: 8, ..default() };
    // Boas-vindas local antes de qualquer mensagem do servidor.
    chat.lines.push((
        "Loomia".into(),
        format!("Bem-vindo(a), {}! Conectado à Baseplate Clássica.", config.player_name),
    ));

    commands
        .spawn((
            Node {
                position_type: PositionType::Absolute,
                left: Val::Px(14.0),
                top: Val::Px(12.0),
                width: Val::Px(460.0),
                flex_direction: FlexDirection::Column,
                row_gap: Val::Px(6.0),
                ..default()
            },
            Name::new("chat-panel"),
            ChatPanel,
        ))
        .with_children(|panel| {
            // Histórico do chat.
            panel
                .spawn((
                    Node {
                        padding: UiRect::all(Val::Px(8.0)),
                        border: UiRect::all(Val::Px(2.0)),
                        min_height: Val::Px(120.0),
                        ..default()
                    },
                    BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.35)),
                    BorderColor(Color::srgba(1.0, 0.85, 0.3, 0.35)),
                ))
                .with_children(|history| {
                    history.spawn((
                        ChatHistoryText,
                        Text::new(""),
                        TextFont { font_size: 15.0, ..default() },
                        TextColor(Color::srgb_u8(240, 240, 240)),
                    ));
                });

            // Campo de entrada (visível só quando o chat está aberto).
            panel
                .spawn((
                    Node {
                        display: Display::None,
                        padding: UiRect::all(Val::Px(6.0)),
                        border: UiRect::all(Val::Px(2.0)),
                        align_items: AlignItems::Center,
                        column_gap: Val::Px(6.0),
                        ..default()
                    },
                    BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.65)),
                    BorderColor(Color::srgb_u8(240, 200, 70)),
                    ChatInputField,
                ))
                .with_children(|field| {
                    field.spawn((
                        Text::new("[Chat]"),
                        TextFont { font_size: 15.0, ..default() },
                        TextColor(Color::srgb_u8(240, 200, 70)),
                    ));
                    field.spawn((
                        Text::new(""),
                        TextFont { font_size: 16.0, ..default() },
                        TextColor(Color::WHITE),
                        ChatInputText,
                    ));
                });
        });

    // Rodapé: controles + diagnóstico.
    commands.spawn((
        Node {
            position_type: PositionType::Absolute,
            left: Val::Px(14.0),
            bottom: Val::Px(10.0),
            flex_direction: FlexDirection::Column,
            row_gap: Val::Px(2.0),
            ..default()
        },
        children![
            (
                HelpText,
                Text::new("WASD / setas: andar · Espaço: pular · Botão direito: girar câmera · Roda: zoom · V: 1ª pessoa · T: chat · R: respawn"),
                TextFont { font_size: 13.0, ..default() },
                TextColor(Color::srgba(1.0, 1.0, 1.0, 0.75)),
            ),
            (
                DiagnosticsText,
                Text::new(""),
                TextFont { font_size: 13.0, ..default() },
                TextColor(Color::srgb_u8(255, 230, 150)),
            ),
        ],
    ));

    // Placar (canto superior direito).
    commands
        .spawn((
            Node {
                position_type: PositionType::Absolute,
                right: Val::Px(14.0),
                top: Val::Px(12.0),
                padding: UiRect::all(Val::Px(8.0)),
                border: UiRect::all(Val::Px(2.0)),
                flex_direction: FlexDirection::Column,
                row_gap: Val::Px(4.0),
                ..default()
            },
            BackgroundColor(Color::srgba(0.0, 0.0, 0.0, 0.35)),
            BorderColor(Color::srgba(1.0, 0.85, 0.3, 0.35)),
        ))
        .with_children(|panel| {
            panel.spawn((
                Text::new("JOGADORES"),
                TextFont { font_size: 14.0, ..default() },
                TextColor(Color::srgb_u8(240, 200, 70)),
            ));
            panel.spawn((
                PlayerListText,
                Text::new(""),
                TextFont { font_size: 14.0, ..default() },
                TextColor(Color::srgb_u8(235, 235, 235)),
            ));
        });

    commands.insert_resource(chat);
}

#[derive(Component)]
pub struct ChatInputText;

fn despawn_hud(mut commands: Commands, entities: Query<Entity, Or<(With<ChatPanel>, With<PlayerListText>, With<HelpText>)>>) {
    for entity in &entities {
        commands.entity(entity).despawn();
    }
    commands.remove_resource::<ChatState>();
}

fn collect_chat_lines(
    mut events: EventReader<ChatLine>,
    mut chat: ResMut<ChatState>,
    mut players: EventReader<ServerPlayers>,
    mut cache: ResMut<AvatarCache>,
) {
    for line in events.read() {
        chat.lines.push((line.from.clone(), line.text.clone()));
        if chat.lines.len() > chat.max_lines {
            chat.lines.remove(0);
        }
    }
    for event in players.read() {
        match event {
            ServerPlayers::Joined(p) => {
                cache.latest.insert(p.id, p.clone());
            }
            ServerPlayers::Left(id) => {
                cache.latest.remove(id);
            }
        }
    }
}

fn toggle_and_type_chat(
    keyboard: Res<ButtonInput<KeyCode>>,
    mut events: EventReader<bevy::input::keyboard::KeyboardInput>,
    mut chat: ResMut<ChatState>,
    outbound: Res<Outbound>,
) {
    if !chat.open {
        if keyboard.just_pressed(KeyCode::KeyT) || keyboard.just_pressed(KeyCode::Enter) {
            chat.open = true;
            chat.input.clear();
        }
        return;
    }

    // Esc fecha sem enviar.
    if keyboard.just_pressed(KeyCode::Escape) {
        chat.open = false;
        chat.input.clear();
        return;
    }

    // Enter envia.
    if keyboard.just_pressed(KeyCode::Enter) || keyboard.just_pressed(KeyCode::NumpadEnter) {
        let text = chat.input.trim().to_string();
        if !text.is_empty() {
            let text: String = text.chars().take(MAX_CHAT_LEN).collect();
            let _ = outbound.tx.send(ClientMessage::Chat { text: text.clone() });
            // Eco local imediato (o servidor também reenvia; filtramos duplicados).
            chat.lines.push(("Você".into(), text));
            if chat.lines.len() > chat.max_lines {
                chat.lines.remove(0);
            }
        }
        chat.open = false;
        chat.input.clear();
        return;
    }

    // Backspace.
    if keyboard.just_pressed(KeyCode::Backspace) {
        chat.input.pop();
    }

    for ev in events.read() {
        if ev.state != bevy::input::ButtonState::Pressed {
            continue;
        }
        if let bevy::input::keyboard::Key::Character(chars) = &ev.logical_key {
            for c in chars.chars() {
                if !c.is_control() && chat.input.chars().count() < MAX_CHAT_LEN {
                    chat.input.push(c);
                }
            }
        }
        if ev.logical_key == bevy::input::keyboard::Key::Space && chat.input.chars().count() < MAX_CHAT_LEN {
            chat.input.push(' ');
        }
    }
}

fn update_hud_texts(
    chat: Res<ChatState>,
    status: Res<NetStatus>,
    cache: Res<AvatarCache>,
    local: Res<LocalPlayer>,
    time: Res<Time>,
    mut history: Query<&mut Text, With<ChatHistoryText>>,
    mut input_text: Query<&mut Text, (With<ChatInputText>, Without<ChatHistoryText>)>,
    mut input_field: Query<&mut Node, With<ChatInputField>>,
    mut player_list: Query<&mut Text, (With<PlayerListText>, Without<ChatHistoryText>, Without<ChatInputText>)>,
    mut diag: Query<&mut Text, (With<DiagnosticsText>, Without<ChatHistoryText>, Without<ChatInputText>, Without<PlayerListText>)>,
) {
    // Histórico.
    if let Ok(mut text) = history.single_mut() {
        let rendered: String = chat
            .lines
            .iter()
            .map(|(from, msg)| format!("[{from}]: {msg}\n"))
            .collect();
        **text = rendered;
    }

    // Campo de entrada.
    if let Ok(mut text) = input_text.single_mut() {
        **text = if chat.open {
            format!("{}_", chat.input)
        } else {
            String::new()
        };
    }
    if let Ok(mut node) = input_field.single_mut() {
        node.display = if chat.open { Display::Flex } else { Display::None };
    }

    // Lista de jogadores (ordenada por nome; o local tem um marcador).
    if let Ok(mut text) = player_list.single_mut() {
        let mut names: Vec<(bool, String)> = cache
            .latest
            .values()
            .map(|p| (Some(p.id) == local.player_id(), p.appearance.username.clone()))
            .collect();
        names.sort_by(|a, b| a.1.to_lowercase().cmp(&b.1.to_lowercase()));
        let rendered = names
            .iter()
            .map(|(is_local, name)| if *is_local { format!("▶ {name}") } else { format!("   {name}") })
            .collect::<Vec<_>>()
            .join("\n");
        **text = format!("{rendered}\n\nonline: {}", names.len());
    }

    // Diagnóstico.
    if let Ok(mut text) = diag.single_mut() {
        let conn = match status.state {
            ConnState::Connecting => "conectando",
            ConnState::Connected => "online",
            ConnState::Closed => "desconectado",
        };
        let ping = status.ping_ms.map(|p| format!("{p:.0} ms")).unwrap_or_else(|| "-- ms".into());
        let uptime = time.elapsed().as_secs();
        **text = format!(
            "Loomia · {conn} · ping {ping} · sessao {uptime}s · {} jogadores",
            cache.latest.len()
        );
    }
}
