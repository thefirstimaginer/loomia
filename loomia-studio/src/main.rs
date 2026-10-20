//! Lançador do Loomia Studio: executa `loomia-studio/Main.luau` na engine Ruzit.
//!
//! O editor inteiro vive em Luau (`Main.luau` + `editor/*.luau`). Este binário
//! só monta o `Fs` do projeto em modo "teste" (lê os `.luau` do disco) e chama
//! `runtime::run_entry` — exatamente como `ruzit test` faria.

use std::path::PathBuf;
use std::process::ExitCode;
use std::sync::Arc;

use ruzit::package;
use ruzit::vfs::{Fs, LazyPackageRegistry, Package};

const PROJECT_ID: &str = "LoomiaStudio";
const ENTRY: &str = "Main.luau";

fn project_root() -> PathBuf {
    if let Ok(dir) = std::env::var("LOOMIA_STUDIO_DIR") {
        return PathBuf::from(dir);
    }
    let bundled = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    if bundled.join(ENTRY).is_file() {
        return bundled;
    }
    if let Ok(exe) = std::env::current_exe() {
        if let Some(dir) = exe.parent() {
            if dir.join(ENTRY).is_file() {
                return dir.to_path_buf();
            }
        }
    }
    bundled
}

fn load_project(root: &std::path::Path, entry: &str) -> Result<Fs, String> {
    let (files, assets) = package::collect_project(root)?;
    let package = Package {
        id: PROJECT_ID.to_string(),
        name: "Loomia Studio".to_string(),
        version: env!("CARGO_PKG_VERSION").to_string(),
        creator: "Loomia".to_string(),
        entry: entry.to_string(),
        file_type: ruzit::config::FileType::Relative,
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
        file_type: ruzit::config::FileType::Relative,
        physical_root: root.to_path_buf(),
    })
}

fn main() -> ExitCode {
    run()
}

fn run() -> ExitCode {
    let root = project_root();
    let fs = match load_project(&root, ENTRY) {
        Ok(fs) => fs,
        Err(err) => {
            eprintln!("[Studio] falha ao ler {}: {err}", root.display());
            return ExitCode::FAILURE;
        }
    };
    println!("[Studio] projeto: {}", root.display());
    match ruzit::runtime::run_entry(fs, ENTRY) {
        Ok(()) => ExitCode::SUCCESS,
        Err(err) => {
            eprintln!("[Studio] erro: {err}");
            ExitCode::FAILURE
        }
    }
}
