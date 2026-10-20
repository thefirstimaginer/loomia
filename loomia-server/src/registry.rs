//! Registro persistente de jogadores.
//!
//! No protótipo é um arquivo JSON (`data/players.json`), mas toda a persistência
//! fica isolada aqui: trocar por PostgreSQL/SQLx significa reescrever apenas
//! este módulo. O formato salvo é a própria [`Appearance`], útil para o site
//! listar os avatares e para o cliente lembrar das cores do último acesso.

use std::path::PathBuf;

use loomia_shared::player::Appearance;
use tokio::sync::Mutex;

use crate::log;

pub struct Registry {
    path: std::path::PathBuf,
    players: Mutex<Vec<Appearance>>,
    loaded: Mutex<bool>,
}

impl Registry {
    pub fn new() -> Self {
        let root = PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .parent()
            .map(PathBuf::from)
            .unwrap_or_else(|| PathBuf::from("."));
        let path = std::env::var("LOOMIA_DATA_DIR")
            .map(PathBuf::from)
            .unwrap_or_else(|_| root.join("data"))
            .join("players.json");
        Self { path, players: Mutex::new(Vec::new()), loaded: Mutex::new(false) }
    }

    async fn ensure_loaded(&self) {
        let mut loaded = self.loaded.lock().await;
        if *loaded {
            return;
        }
        if let Ok(text) = tokio::fs::read_to_string(&self.path).await {
            match serde_json::from_str::<Vec<Appearance>>(&text) {
                Ok(players) => *self.players.lock().await = players,
                Err(err) => log::warn(&format!("players.json inválido: {err}")),
            }
        }
        *loaded = true;
    }

    pub async fn upsert(&self, appearance: Appearance) {
        self.ensure_loaded().await;
        {
            let mut players = self.players.lock().await;
            if let Some(existing) = players.iter_mut().find(|p| p.username == appearance.username) {
                *existing = appearance.clone();
            } else {
                players.push(appearance.clone());
            }
            if players.len() > 500 {
                players.remove(0);
            }
        }
        if let Err(err) = self.save().await {
            log::warn(&format!("não consegui salvar players.json: {err}"));
        }
    }

    pub async fn all(&self) -> Vec<Appearance> {
        self.ensure_loaded().await;
        self.players.lock().await.clone()
    }

    async fn save(&self) -> std::io::Result<()> {
        if let Some(dir) = self.path.parent() {
            tokio::fs::create_dir_all(dir).await?;
        }
        let players = self.players.lock().await;
        let text = serde_json::to_string_pretty(&*players).unwrap_or_default();
        tokio::fs::write(&self.path, text).await
    }
}

impl Default for Registry {
    fn default() -> Self {
        Self::new()
    }
}
