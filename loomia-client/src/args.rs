//! Argumentos de linha de comando do cliente.
//!
//! ```text
//! loomia-client [--server ws://host:porta/ws] [--name Nome] [--world id]
//!               [--autoplay] [--exit-after SEGUNDOS]
//! ```
//!
//! Variáveis de ambiente equivalentes: `LOOMIA_SERVER`, `LOOMIA_NAME`.

use std::env;

#[derive(Debug, Clone)]
pub struct LaunchOptions {
    pub server: String,
    pub name: String,
    pub world: Option<String>,
    pub autoplay: bool,
    pub exit_after: Option<f32>,
    pub appearance: Option<loomia_shared::player::Appearance>,
}

pub fn parse() -> LaunchOptions {
    let mut opts = LaunchOptions {
        server: env::var("LOOMIA_SERVER").unwrap_or_else(|_| "ws://127.0.0.1:8080/ws".into()),
        name: env::var("LOOMIA_NAME").unwrap_or_else(|_| default_name()),
        world: None,
        autoplay: false,
        exit_after: None,
        appearance: None,
    };

    let mut args = env::args().skip(1);
    while let Some(arg) = args.next() {
        let mut value = |flag: &str| {
            args.next()
                .unwrap_or_else(|| panic!("{flag} precisa de um valor"))
        };
        match arg.as_str() {
            "--server" | "-s" => opts.server = value("--server"),
            "--name" | "-n" => opts.name = value("--name"),
            "--world" | "-w" => opts.world = Some(value("--world")),
            "--autoplay" => opts.autoplay = true,
            "--appearance" => {
                let json = value("--appearance");
                match serde_json::from_str::<loomia_shared::player::Appearance>(&json) {
                    Ok(appearance) => opts.appearance = Some(appearance),
                    Err(err) => eprintln!("--appearance inválida: {err}"),
                }
            }
            "--exit-after" => {
                opts.exit_after = Some(value("--exit-after").parse().unwrap_or(30.0));
            }
            "--help" | "-h" => {
                println!(
                    "Loomia client\n\nUso: loomia-client [--server URL] [--name NOME] [--world ID]"
                );
                std::process::exit(0);
            }
            "--version" | "-V" => {
                println!("loomia-client {}", env!("CARGO_PKG_VERSION"));
                std::process::exit(0);
            }
            other => eprintln!("argumento desconhecido ignorado: {other}"),
        }
    }

    // Viewers usam "file://" ou recebem um nome estranho: sanitiza.
    opts.name = opts
        .name
        .chars()
        .filter(|c| !c.is_control())
        .take(20)
        .collect();
    if opts.name.trim().is_empty() {
        opts.name = default_name();
    }
    opts
}

fn default_name() -> String {
    let n: u32 = rand::random_range(1..=9999);
    format!("Loomian{n}")
}
