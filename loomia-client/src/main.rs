//! Cliente nativo do Loomia — **lançador** do projeto Ruzit.
//!
//! O jogo em si vive em `loomia-client/game/*.luau` e roda sobre a engine
//! Ruzit (`Ruzit/Ruzit/runtime`), compilada como biblioteca e embutida neste
//! binário. Não há mais Bevy: aqui só traduzimos os argumentos antigos
//! (`--server`, `--name`, `--world`, `--appearance`) para variáveis de
//! ambiente lidas pelo `game/config.luau` e chamamos `runtime::run_entry`.
//!
//! O mapa **não** é carregado do disco: o cliente recebe o `.loom` pelo
//! servidor (WebSocket) e apenas o renderiza.

use std::env;
use std::path::{Path, PathBuf};
use std::process::ExitCode;
use std::sync::Arc;

use loomia_shared::player::Appearance;
use ruzit::config::FileType;
use ruzit::package;
use ruzit::vfs::{Fs, LazyPackageRegistry, Package};

/// Pacote padrão do projeto (a engine chama isso de `@Game`).
const PROJECT_ID: &str = "Loomia";
const ENTRY: &str = "Main.luau";

/// Pasta do projeto Ruzit.
fn project_root() -> PathBuf {
    if let Ok(dir) = env::var("LOOMIA_GAME_DIR") {
        return PathBuf::from(dir);
    }
    // Rodando via `cargo run`: a pasta `game/` fica ao lado deste crate.
    let bundled = Path::new(env!("CARGO_MANIFEST_DIR")).join("game");
    if bundled.is_dir() {
        return bundled;
    }
    // Binário distribuído: `game/` ao lado do executável, ou na raiz do repo.
    if let Ok(exe) = env::current_exe() {
        if let Some(dir) = exe.parent() {
            let next_to_exe = dir.join("game");
            if next_to_exe.is_dir() {
                return next_to_exe;
            }
            if let Some(root) = dir.parent().and_then(|p| p.parent()) {
                let candidate = root.join("loomia-client").join("game");
                if candidate.is_dir() {
                    return candidate;
                }
            }
        }
    }
    bundled
}

struct Options {
    server: String,
    name: String,
    world: Option<String>,
    appearance: Option<Appearance>,
}

fn parse_args() -> Options {
    let mut opts = Options {
        server: env::var("LOOMIA_SERVER").unwrap_or_else(|_| "ws://127.0.0.1:8080/ws".into()),
        name: env::var("LOOMIA_NAME").unwrap_or_else(|_| "Jogador".into()),
        world: None,
        appearance: None,
    };

    let mut args = env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--server" | "-s" => {
                if let Some(v) = args.next() {
                    opts.server = v;
                }
            }
            "--name" | "-n" => {
                if let Some(v) = args.next() {
                    opts.name = v;
                }
            }
            "--world" | "-w" => opts.world = args.next(),
            "--appearance" => {
                if let Some(json) = args.next() {
                    match serde_json::from_str::<Appearance>(&json) {
                        Ok(appearance) => opts.appearance = Some(appearance),
                        Err(err) => eprintln!("--appearance inválida: {err}"),
                    }
                }
            }
            // Aceitos pelo launcher antigo; a engine não usa.
            "--autoplay" => {}
            "--exit-after" => {
                args.next();
            }
            "--help" | "-h" => {
                println!(
                    "Loomia client (Ruzit)\n\n\
                     Uso: loomia-client [--server ws://host:8080/ws] [--name NOME] [--world ID]"
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

    opts.name = opts
        .name
        .chars()
        .filter(|c| !c.is_control())
        .take(20)
        .collect();
    if opts.name.trim().is_empty() {
        opts.name = "Jogador".into();
    }
    opts
}

/// Monta o `Fs` do projeto em "modo teste": lê os `.luau` direto do disco.
fn load_project(root: &Path, entry: &str) -> Result<Fs, String> {
    let (files, assets) = package::collect_project(root)?;
    let package = Package {
        id: PROJECT_ID.to_string(),
        name: "Loomia".to_string(),
        version: env!("CARGO_PKG_VERSION").to_string(),
        creator: "Loomia".to_string(),
        entry: entry.to_string(),
        file_type: FileType::Relative,
        physical_root: Some(root.to_path_buf()),
        files,
        assets: std::sync::RwLock::new(Some(package::encode_assets_b64(assets))),
        scripts_compressed: false,
        scripts_bytecode: false,
        encryption_token: String::new(),
        sources: Vec::new(),
        activated: std::sync::atomic::AtomicBool::new(true),
        is_default: true,
        package_flags: Vec::new(),
        pending_shards: std::sync::RwLock::new(Vec::new()),
    };
    let mut registry = LazyPackageRegistry::new_test_mode();
    registry.insert_ready(PROJECT_ID.to_string(), Arc::new(package));
    Ok(Fs::Bundle {
        packages: Arc::new(registry),
        default_id: PROJECT_ID.to_string(),
        file_type: FileType::Relative,
        physical_root: root.to_path_buf(),
    })
}

fn main() -> ExitCode {
    let opts = parse_args();
    let root = project_root();
    println!(
        "Loomia client {} (Ruzit) — servidor {} — jogador '{}'",
        env!("CARGO_PKG_VERSION"),
        opts.server,
        opts.name
    );

    run(opts, root)
}

fn run(opts: Options, root: PathBuf) -> ExitCode {
    if !root.is_dir() {
        eprintln!(
            "[Loomia] projeto Ruzit não encontrado em {}",
            root.display()
        );
        return ExitCode::FAILURE;
    }

    // Lidos por `game/config.luau` via `Process.Env`/`import`. Em Rust 2024
    // `set_var` é unsafe (ainda estamos numa thread só, antes da engine subir).
    unsafe {
        env::set_var("LOOMIA_SERVER_URL", &opts.server);
        env::set_var("LOOMIA_PLAYER_NAME", &opts.name);
        if let Some(world) = &opts.world {
            env::set_var("LOOMIA_WORLD", world);
        }
        if let Some(appearance) = &opts.appearance {
            if let Ok(json) = serde_json::to_string(appearance) {
                env::set_var("LOOMIA_APPEARANCE", json);
            }
        }
    }

    let fs = match load_project(&root, ENTRY) {
        Ok(fs) => fs,
        Err(err) => {
            eprintln!("[Loomia] falha ao ler {}: {err}", root.display());
            return ExitCode::FAILURE;
        }
    };

    match ruzit::runtime::run_entry(fs, ENTRY) {
        Ok(()) => ExitCode::SUCCESS,
        Err(err) => {
            eprintln!("[Loomia] erro: {err}");
            ExitCode::FAILURE
        }
    }
}
