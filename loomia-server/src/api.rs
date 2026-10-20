//! API web do Loomia consumida pelo frontend.
//!
//! | Rota                    | Descrição                                 |
//! |-------------------------|-------------------------------------------|
//! | `GET  /api/health`      | sonda de vida                              |
//! | `GET  /api/status`      | versão, uptime, jogadores, mapa, latência  |
//! | `GET  /api/worlds`      | catálogo de mundos                         |
//! | `GET  /api/worlds/{id}` | mapa completo (blocos)                     |
//! | `POST /api/player`      | registra/atualiza um avatar                |
//! | `GET  /api/players`     | avatares já registrados                    |
//! | `POST /api/launch`      | inicia o cliente nativo (botão LAUNCH)     |
//! | `GET  /api/launch/status` | progresso da compilação do cliente       |

use std::{net::{IpAddr, SocketAddr}, path::PathBuf, sync::Arc};

use axum::{extract::{ConnectInfo, State}, http::StatusCode, Json};
use loomia_shared::player::Appearance;
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

use crate::{
    game::{Game, TICK_RATE},
    launcher::{LaunchRequest, Launcher},
    VERSION,
};

#[derive(Clone)]
pub struct AppState {
    pub game: Arc<Game>,
    pub launcher: Arc<Launcher>,
    pub frontend_dir: PathBuf,
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct WorldInfo {
    pub id: String,
    pub name: String,
    pub description: String,
    pub max_players: u32,
    #[serde(default)]
    pub player_count: u32,
}

type ApiError = (StatusCode, Json<Value>);

fn bad_request(msg: impl Into<String>) -> ApiError {
    (StatusCode::BAD_REQUEST, Json(json!({ "error": msg.into() })))
}

pub async fn health() -> Json<Value> {
    Json(json!({ "ok": true, "service": "loomia-server", "version": VERSION }))
}

pub async fn status(State(state): State<AppState>) -> Json<Value> {
    let game = &state.game;
    let (map_name, blocks) = game.map_summary().await;
    let players = game.online_players().await;
    let frontend_ready = state.frontend_dir.join("index.html").exists();

    Json(json!({
        "version": VERSION,
        "uptime_secs": game.uptime_secs(),
        "tick_rate": TICK_RATE,
        "ws_url": game.ws_url(),
        "players_online": players.len(),
        "sessions": game.session_count().await,
        "total_connections": game.total_connections.load(std::sync::atomic::Ordering::Relaxed),
        "total_chat_messages": game.total_chat_messages.load(std::sync::atomic::Ordering::Relaxed),
        "map": { "name": map_name, "blocks": blocks },
        "players": players.iter().map(|p| json!({
            "id": p.id,
            "name": p.appearance.username,
            "position": p.position,
        })).collect::<Vec<_>>(),
        "client": {
            "built": state.launcher.is_built(),
            "path": state.launcher.client_binary().display().to_string(),
        },
        "frontend": {
            "dir": state.frontend_dir.display().to_string(),
            "ready": frontend_ready,
        },
    }))
}

pub async fn worlds(State(state): State<AppState>) -> Json<Vec<WorldInfo>> {
    Json(state.game.worlds().await)
}

pub async fn world(
    State(state): State<AppState>,
    axum::extract::Path(id): axum::extract::Path<String>,
) -> Result<Json<loomia_shared::world::WorldMap>, ApiError> {
    let map = state.game.map_clone().await;
    if map.id != id && id != "baseplate" {
        return Err((StatusCode::NOT_FOUND, Json(json!({ "error": "mundo não encontrado" }))));
    }
    Ok(Json(map))
}

pub async fn register_player(
    State(state): State<AppState>,
    Json(appearance): Json<Appearance>,
) -> Json<Appearance> {
    Json(state.game.register_player(appearance).await)
}

pub async fn list_players(State(state): State<AppState>) -> Json<Vec<Appearance>> {
    Json(state.game.known_players().await)
}

pub async fn launch_status(State(state): State<AppState>) -> Json<Value> {
    let progress = state.launcher.progress().await;
    Json(json!({
        "built": state.launcher.is_built(),
        "building": progress.building,
        "last_line": progress.last_line,
        "binary": state.launcher.client_binary().display().to_string(),
    }))
}

#[derive(Deserialize, Debug)]
#[serde(rename_all = "camelCase")]
pub struct LaunchPayload {
    #[serde(default)]
    pub player_name: Option<String>,
    #[serde(default)]
    pub world: Option<String>,
}

/// Inicia o cliente 3D nesta máquina.
///
/// Por segurança, só é permitido quando a requisição vem da própria máquina
/// (loopback) ou quando `LOOMIA_ALLOW_LAUNCH=1` está definida.
pub async fn launch(
    State(state): State<AppState>,
    ConnectInfo(addr): ConnectInfo<SocketAddr>,
    body: Option<Json<LaunchPayload>>,
) -> Result<Json<Value>, ApiError> {
    let from_local = addr.ip().is_loopback()
        || matches!(addr.ip(), IpAddr::V4(v4) if v4.is_private());
    if std::env::var("LOOMIA_ALLOW_LAUNCH").as_deref() != Ok("1") && !from_local {
        return Err((
            StatusCode::FORBIDDEN,
            Json(json!({ "error": "launch só é permitido a partir da máquina local" })),
        ));
    }

    let payload = body.map(|Json(p)| p).unwrap_or(LaunchPayload { player_name: None, world: None });
    let player_name = payload.player_name.map(|n| n.chars().take(20).collect::<String>());

    let req = LaunchRequest {
        server_url: state.game.ws_url(),
        player_name,
        world: payload.world,
    };

    match state.launcher.launch(req).await {
        Ok(outcome) => Ok(Json(json!({
            "ok": true,
            "pid": outcome.pid,
            "message": outcome.message,
            "built_now": outcome.built_now,
        }))),
        Err(err) => Err((
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(json!({ "error": format!("{err}") })),
        )),
    }
}

fn _unused_is_loopback(headers: &axum::http::HeaderMap) -> bool {
    // X-Forwarded-For é ignorado de propósito (pode ser forjado).
    headers
        .get("x-real-ip")
        .and_then(|v| v.to_str().ok())
        .map(|ip| ip == "127.0.0.1" || ip == "::1")
        .unwrap_or(true)
        && headers
            .get("origin")
            .and_then(|v| v.to_str().ok())
            .map(|origin| {
                origin.contains("localhost") || origin.contains("127.0.0.1") || origin.starts_with("http://[::1]")
            })
            .unwrap_or(true)
}

#[allow(dead_code)]
fn not_found() -> ApiError {
    (StatusCode::NOT_FOUND, Json(json!({ "error": "não encontrado" })))
}
