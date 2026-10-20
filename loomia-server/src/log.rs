//! Logging mínimo com timestamps, sem crates extras.
//!
//! `LOOMIA_LOG=debug|info|warn|error` controla o nível (padrão `info`).

use std::{
    sync::OnceLock,
    time::{SystemTime, UNIX_EPOCH},
};

#[derive(Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Level {
    Error,
    Warn,
    Info,
    Debug,
}

fn threshold() -> Level {
    static LEVEL: OnceLock<Level> = OnceLock::new();
    *LEVEL.get_or_init(|| {
        match std::env::var("LOOMIA_LOG")
            .unwrap_or_default()
            .to_lowercase()
            .as_str()
        {
            "debug" | "trace" | "verbose" => Level::Debug,
            "warn" | "warning" => Level::Warn,
            "error" | "quiet" => Level::Error,
            _ => Level::Info,
        }
    })
}

fn timestamp() -> String {
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default();
    let secs = now.as_secs();
    let (h, m, s) = ((secs / 3600) % 24, (secs / 60) % 60, secs % 60);
    format!("{:02}:{:02}:{:02}", h, m, s)
}

pub fn log(level: Level, tag: &str, msg: &str) {
    if level > threshold() {
        return;
    }
    println!("[{}] {:>5} {}", timestamp(), tag, msg);
}

pub fn info(msg: &str) {
    log(Level::Info, "info", msg);
}

pub fn warn(msg: &str) {
    log(Level::Warn, "warn", msg);
}

pub fn error(msg: &str) {
    log(Level::Error, "error", msg);
}

pub fn debug(msg: &str) {
    log(Level::Debug, "debug", msg);
}
