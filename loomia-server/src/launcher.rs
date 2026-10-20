//! Inicialização do cliente nativo a partir do site (botão "LAUNCH").
//!
//! O fluxo do launcher:
//!
//! 1. `/api/launch` chama [`Launcher::launch`], que **compila** o cliente se
//!    necessário (`cargo build -p loomia-client`) e depois o inicia destacado,
//!    passando `--server ws://host:port/ws --name <jogador>`.
//! 2. O cliente mostra a janela "Carregando Loomia, Aguarde!" enquanto conecta.
//!
//! Para usar um binário já existente (release, instalado no PATH etc.), defina
//! `LOOMIA_CLIENT_COMMAND` — ex.: `LOOMIA_CLIENT_COMMAND=/usr/local/bin/loomia-client`.
//!
//! Segurança: o endpoint só inicia processos locais; por padrão o servidor escuta
//! em `127.0.0.1`. Se rodar exposto na rede, mantenha `LOOMIA_ALLOW_LAUNCH=1`
//! apenas em máquinas confiáveis (ou não defina a variável e o endpoint retorna 403).

use std::{
    path::{Path, PathBuf},
    process::Stdio,
    sync::{
        atomic::{AtomicBool, Ordering},
        Arc,
    },
    time::{Duration, Instant},
};

use tokio::{process::Command, sync::Mutex};
use crate::log;

#[derive(Clone, Debug)]
pub struct LaunchRequest {
    pub server_url: String,
    pub player_name: Option<String>,
    pub world: Option<String>,
}

#[derive(Clone, Debug)]
pub struct LaunchOutcome {
    pub pid: u32,
    pub message: String,
    pub built_now: bool,
}

#[derive(Clone, Debug)]
pub struct LaunchProgress {
    pub building: bool,
    pub last_line: String,
    pub finished_at: Option<Instant>,
}

#[derive(Clone)]
pub struct Launcher {
    inner: Arc<LauncherInner>,
}

struct LauncherInner {
    custom_command: Option<String>,
    project_root: PathBuf,
    bin_path: PathBuf,
    built: AtomicBool,
    building: AtomicBool,
    progress: Mutex<LaunchProgress>,
}

impl Launcher {
    pub fn new(custom_command: Option<String>, project_root: PathBuf) -> Self {
        let bin_name = if cfg!(windows) { "loomia-client.exe" } else { "loomia-client" };
        let bin_path = project_root.join("target").join("debug").join(bin_name);
        Self {
            inner: Arc::new(LauncherInner {
                custom_command,
                project_root,
                bin_path,
                built: AtomicBool::new(false),
                building: AtomicBool::new(false),
                progress: Mutex::new(LaunchProgress { building: false, last_line: String::new(), finished_at: None }),
            }),
        }
    }

    pub fn client_binary(&self) -> PathBuf {
        self.inner.bin_path.clone()
    }

    pub fn project_root(&self) -> &Path {
        &self.inner.project_root
    }

    pub fn is_built(&self) -> bool {
        self.inner.custom_command.is_some() || self.inner.bin_path.exists()
    }

    /// Compila o cliente se necessário. Idempotente e seguro para chamadas
    /// concorrentes (apenas uma compilação roda por vez).
    pub async fn ensure_built(&self) -> anyhow_lite::Result<()> {
        if self.inner.custom_command.is_some() || self.inner.bin_path.exists() {
            self.inner.built.store(true, Ordering::SeqCst);
            return Ok(());
        }
        if self.inner.building.swap(true, Ordering::SeqCst) {
            // Outra compilação está em andamento: espera terminar.
            while self.inner.building.load(Ordering::SeqCst) {
                tokio::time::sleep(Duration::from_millis(500)).await;
            }
            return if self.inner.bin_path.exists() {
                Ok(())
            } else {
                Err(anyhow_lite::err("compilação do cliente falhou (veja o log do servidor)"))
            };
        }

        log::info("compilando o cliente (cargo build -p loomia-client) — isso pode levar alguns minutos na primeira vez");
        {
            let mut progress = self.inner.progress.lock().await;
            progress.building = true;
            progress.last_line = "cargo build -p loomia-client".into();
        }

        let mut child = Command::new("cargo")
            .args(["build", "-p", "loomia-client"])
            .current_dir(&self.inner.project_root)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .map_err(|err| anyhow_lite::err(format!("não consegui executar cargo: {err}")))?;

        let stdout = tokio::io::BufReader::new(child.stdout.take().unwrap());
        let stderr = tokio::io::BufReader::new(child.stderr.take().unwrap());
        // Os dois streams são lidos em tasks separadas: um `select!` entre eles
        // poderia travara o loop quando um stream termina antes do outro.
        let progress_out = self.clone();
        let progress_err = self.clone();
        let out_task = tokio::spawn(async move {
            use tokio::io::AsyncBufReadExt;
            let mut lines = stdout.lines();
            while let Ok(Some(line)) = lines.next_line().await {
                progress_out.note_progress(line).await;
            }
        });
        let err_task = tokio::spawn(async move {
            use tokio::io::AsyncBufReadExt;
            let mut lines = stderr.lines();
            while let Ok(Some(line)) = lines.next_line().await {
                progress_err.note_progress(line).await;
            }
        });
        let status = child.wait().await;
        let _ = tokio::join!(out_task, err_task);

        self.inner.building.store(false, Ordering::SeqCst);
        {
            let mut progress = self.inner.progress.lock().await;
            progress.building = false;
            progress.finished_at = Some(Instant::now());
        }

        match status {
            Ok(s) if s.success() && self.inner.bin_path.exists() => {
                self.inner.built.store(true, Ordering::SeqCst);
                log::info("cliente compilado com sucesso");
                Ok(())
            }
            Ok(s) => Err(anyhow_lite::err(format!("cargo build saiu com código {s}"))),
            Err(err) => Err(anyhow_lite::err(format!("falha ao aguardar cargo: {err}"))),
        }
    }

    async fn note_progress(&self, line: String) {
        let mut progress = self.inner.progress.lock().await;
        if !line.trim().is_empty() {
            progress.last_line = line;
        }
    }

    pub async fn progress(&self) -> LaunchProgress {
        self.inner.progress.lock().await.clone()
    }

    /// Inicia o cliente destacado (não herda stdio; ver `data/logs/client.log`).
    pub async fn launch(&self, req: LaunchRequest) -> anyhow_lite::Result<LaunchOutcome> {
        let built_now = !self.is_built() && !self.inner.building.load(Ordering::SeqCst);
        self.ensure_built().await?;

        let logs_dir = self.inner.project_root.join("data").join("logs");
        let _ = tokio::fs::create_dir_all(&logs_dir).await;
        let log_path = logs_dir.join("client.log");
        // `tungstenite`/Bevy escrevem muita coisa no stderr; guardamos em arquivo
        // para não poluir o terminal do servidor.
        let log_out = std::fs::OpenOptions::new().create(true).append(true).open(&log_path).ok();
        let log_err = std::fs::OpenOptions::new().create(true).append(true).open(&log_path).ok();

        let mut cmd = match &self.inner.custom_command {
            Some(custom) => {
                let parts: Vec<&str> = custom.split_whitespace().collect();
                let mut c = Command::new(parts.first().copied().unwrap_or("loomia-client"));
                c.args(&parts[1..]);
                c
            }
            None => Command::new(&self.inner.bin_path),
        };
        cmd.arg("--server").arg(&req.server_url);
        if let Some(name) = &req.player_name {
            cmd.arg("--name").arg(name);
        }
        if let Some(world) = &req.world {
            cmd.arg("--world").arg(world);
        }
        cmd.stdin(Stdio::null());
        if let Some(out) = log_out {
            cmd.stdout(Stdio::from(out));
        } else {
            cmd.stdout(Stdio::null());
        }
        if let Some(err) = log_err {
            cmd.stderr(Stdio::from(err));
        } else {
            cmd.stderr(Stdio::null());
        }
        if let Some(world_id) = req.world.as_deref() {
            log::info(&format!("cliente solicitado para o mundo '{world_id}'"));
        }

        // Destaca o processo: em Unix, um novo grupo de processos para que o
        // cliente sobreviva ao servidor.
        #[cfg(unix)]
        {
            use std::os::unix::process::CommandExt;
            cmd.as_std_mut().process_group(0);
        }

        let child = cmd.spawn().map_err(|err| anyhow_lite::err(format!("não consegui iniciar o cliente: {err}")))?;
        let pid = child.id().unwrap_or(0);
        log::info(&format!(
            "cliente iniciado (pid {pid}) conectando em {}",
            req.server_url
        ));

        Ok(LaunchOutcome {
            pid,
            built_now,
            message: "Cliente iniciado! A janela 'Carregando Loomia' deve abrir em instantes.".into(),
        })
    }
}

/// Mini tipo de erro com mensagem (evita dependência de `anyhow`).
pub mod anyhow_lite {
    use std::fmt;

    #[derive(Debug)]
    pub struct Error(pub String);

    impl fmt::Display for Error {
        fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
            f.write_str(&self.0)
        }
    }

    impl std::error::Error for Error {}

    pub type Result<T> = std::result::Result<T, Error>;

    pub fn err(msg: impl Into<String>) -> Error {
        Error(msg.into())
    }
}

