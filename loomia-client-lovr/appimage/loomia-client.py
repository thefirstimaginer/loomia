#!/usr/bin/env python3
"""
Cliente Loomia — controlador local.

Roda dentro do AppImage e é o único processo que o jogador abre. Responsabilidades:

  1. mostrar a janela "Cliente Loomia Pronto, Vá para o site e se divirta!"
  2. manter um ícone na bandeja do sistema (minimizar = continuar rodando);
  3. registrar o protocolo loomia:// UMA única vez (um .desktop + xdg-mime);
  4. receber os links loomia://launch?... (do navegador ou da linha de comando),
     falar com o master para obter a sala e abrir o jogo;
  5. hospedar o transporte UDP local (127.0.0.1) que o engine Lua consome.

Sem dependências de terceiros: só a biblioteca padrão do Python 3, o `tkinter`
(que todo AppImage de desktop tem) e o X11 do sistema.

Uso:
    loomia-client                  abre a janela + bandeja
    loomia-client loomia://launch?server=...&token=...   (chamado pelo navegador)
    loomia-client --register-only  registra o protocolo e sai
    loomia-client --no-tray        sem ícone (para testar)
"""

from __future__ import annotations

import base64
import hashlib
import json
import os
import queue
import re
import shlex
import socket
import struct
import subprocess
import sys
import threading
import time
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

APP_ID = "loomia"
APP_NAME = "Loomia"
APP_VERSION = "1.0.0"
SCHEME = "loomia"

# O jogo (engine Lua) conversa com este endereço; a porta é fixa e local.
BRIDGE_HOST = "127.0.0.1"
BRIDGE_PORT = int(os.environ.get("LOOMIA_BRIDGE_PORT", "7788"))

HOME = Path(os.environ.get("HOME") or str(Path.home()))
APPDIR = Path(os.environ.get("APPDIR", Path(__file__).resolve().parent.parent))
STATE_DIR = Path(os.environ.get("XDG_STATE_HOME", HOME / ".local" / "state")) / APP_ID
CONFIG_DIR = Path(os.environ.get("XDG_CONFIG_HOME", HOME / ".config")) / APP_ID
DATA_DIR = Path(os.environ.get("XDG_DATA_HOME", HOME / ".local" / "share"))
APPLICATIONS_DIR = DATA_DIR / "applications"
ICON_PATH = STATE_DIR / "loomia.svg"
LOCK_PATH = STATE_DIR / "client.lock"
PID_PATH = STATE_DIR / "client.pid"
LOG_PATH = STATE_DIR / "client.log"
LAUNCH_FILE = STATE_DIR / "last-launch.json"
GAME_LOG = STATE_DIR / "game.log"

DEFAULT_MASTER = os.environ.get("LOOMIA_MASTER", "http://127.0.0.1:8080")
DEFAULT_NAME = os.environ.get("LOOMIA_PLAYER", "Loomian")


def app_path() -> Path:
    """Executável a registrar no .desktop: o AppImage, quando empacotado."""
    return Path(os.environ.get("APPIMAGE") or Path(sys.argv[0]).resolve())


def log(message: str) -> None:
    line = f"[{time.strftime('%H:%M:%S')}] {message}"
    print(line, flush=True)
    try:
        STATE_DIR.mkdir(parents=True, exist_ok=True)
        with LOG_PATH.open("a", encoding="utf-8") as handle:
            handle.write(line + "\n")
    except OSError:
        pass


# ---------------------------------------------------------------------------
# Ícone (SVG gerado uma vez; o .desktop aponta para ele)
# ---------------------------------------------------------------------------

ICON_SVG = """<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 16 16">
  <rect x="0" y="0" width="16" height="16" rx="3" fill="#141e30"/>
  <rect x="1" y="1" width="14" height="14" rx="2.5" fill="none" stroke="#eabe19" stroke-width="1.4"/>
  <path d="M4 3.5h2.2v7.2H4z" fill="#0f78be"/>
  <path d="M4 9.4h6.6v1.9H4z" fill="#0f78be"/>
  <circle cx="11.2" cy="4.4" r="1.1" fill="#eabe19"/>
</svg>
"""


def ensure_icon() -> Path:
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    if not ICON_PATH.exists() or ICON_PATH.read_text(encoding="utf-8") != ICON_SVG:
        ICON_PATH.write_text(ICON_SVG, encoding="utf-8")
    return ICON_PATH


# ---------------------------------------------------------------------------
# Registro do protocolo loomia:// (uma única vez)
# ---------------------------------------------------------------------------

def protocol_marker() -> Path:
    return STATE_DIR / "protocol-registered"


def launch_command() -> str:
    """Comando do .desktop.

    `%u` é o link loomia://. Rodar o AppImage direto seria o ideal, mas o caminho
    muda a cada montagem (e os links do navegador levariam a um ponto morto);
    usamos o lançador `loomia-client`, instalado no primeiro clique.
    """
    launcher = Path(HOME) / ".local" / "bin" / "loomia-client"
    return str(launcher) if launcher.exists() else shlex.quote(os.environ.get(
        "LOOMIA_CLIENT_BIN") or str(app_path()))


def desktop_entry(appimage: Path, icon_path: Path) -> str:
    return f"""[Desktop Entry]
Type=Application
Name={APP_NAME}
Comment=Cliente Loomia
Exec={launch_command()} %u
Icon={icon_path}
Terminal=false
StartupWMClass={APP_NAME}
Categories=Game;
MimeType=x-scheme-handler/{SCHEME};
"""


def install_launcher(appimage: Path) -> Path | None:
    """Cria ~/.local/bin/loomia-client apontando para O AppImage.

    Uma única vez; se o caminho do AppImage mudar (o jogador moveu o arquivo),
    o lançador é regravado. Assim o loomia:// sobrevive ao caminho móvel.
    """
    bin_dir = Path(HOME) / ".local" / "bin"
    launcher = bin_dir / "loomia-client"
    content = (
        "#!/bin/sh\n"
        "# Loomia: abre o cliente (gerado automaticamente; aponte para o AppImage).\n"
        f'exec "{appimage}" "$@"\n'
    )
    try:
        bin_dir.mkdir(parents=True, exist_ok=True)
        if launcher.exists() and launcher.read_text(encoding="utf-8") == content:
            return launcher
        launcher.write_text(content, encoding="utf-8")
        launcher.chmod(0o755)
        return launcher
    except OSError:
        return None


def register_protocol(force: bool = False) -> None:
    """Registra loomia:// no desktop. Idempotente: roda só uma vez por instalação."""
    marker = protocol_marker()
    if marker.exists() and not force:
        return

    try:
        appimage = app_path()
        ensure_icon()
        install_launcher(appimage)
        APPLICATIONS_DIR.mkdir(parents=True, exist_ok=True)
        desktop_file = APPLICATIONS_DIR / f"{APP_ID}-client.desktop"
        content = desktop_entry(appimage, ICON_PATH)
        if not desktop_file.exists() or desktop_file.read_text(encoding="utf-8") != content:
            desktop_file.write_text(content, encoding="utf-8")
            desktop_file.chmod(0o755)

        subprocess.run(["xdg-mime", "default", desktop_file.name, f"x-scheme-handler/{SCHEME}"],
                       check=False, capture_output=True)
        subprocess.run(["update-desktop-database", str(APPLICATIONS_DIR)],
                       check=False, capture_output=True)

        marker.parent.mkdir(parents=True, exist_ok=True)
        marker.write_text(f"{desktop_file}\n{appimage}\n", encoding="utf-8")
        log(f"protocolo {SCHEME}:// registrado em {desktop_file}")
        log(f"lançador: {launch_command()}")
    except OSError as error:
        log(f"não foi possível registrar {SCHEME}://: {error}")


# ---------------------------------------------------------------------------
# Instância única
# ---------------------------------------------------------------------------

def acquire_lock() -> bool:
    """Devolve True se esta é a instância principal."""
    STATE_DIR.mkdir(parents=True, exist_ok=True)
    try:
        import fcntl
        handle = LOCK_PATH.open("w")
        fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        handle.write(str(os.getpid()))
        handle.flush()
        global _lock_handle
        _lock_handle = handle
        return True
    except (OSError, ImportError):
        return False


def show_existing() -> bool:
    """Pede à instância principal para mostrar a janela (segundo clique)."""
    try:
        with socket.create_connection(("127.0.0.1", BRIDGE_PORT), timeout=1.5) as connection:
            connection.sendall(json.dumps({"type": "show"}).encode("utf-8") + b"\n")
            reply = connection.recv(256)
            return reply.startswith(b"ok")
    except OSError:
        return False


# ---------------------------------------------------------------------------
# Estado compartilhado
# ---------------------------------------------------------------------------

@dataclass
class ClientState:
    status: str = "Pronto"
    map_name: str = ""
    server: str = ""
    game_pid: int | None = None
    game_started_at: float = 0.0
    last_launch: dict = field(default_factory=dict)
    log_tail: str = ""


STATE = ClientState()
COMMANDS: "queue.Queue[str]" = queue.Queue()


# ---------------------------------------------------------------------------
# Link loomia://
# ---------------------------------------------------------------------------

def parse_launch_link(link: str) -> dict | None:
    match = re.match(r"^loomia:\/*([^?#]*)(?:\?(.*))?$", link.strip().strip('"\''), re.IGNORECASE)
    if not match:
        return None
    query = urllib.parse.parse_qs(match.group(2) or "")
    return {
        "action": match.group(1) or "launch",
        "server": (query.get("server") or [""])[0],
        "token": (query.get("token") or [""])[0],
        "map": (query.get("map") or [""])[0],
        "name": (query.get("name") or [DEFAULT_NAME])[0][:20],
        "color": (query.get("color") or ["eabe19"])[0],
        "origin": (query.get("origin") or [DEFAULT_MASTER])[0],
    }


def launch_from_link(link: str) -> None:
    parsed = parse_launch_link(link)
    if not parsed:
        log(f"link inválido: {link}")
        return
    if parsed["action"] not in ("launch", "play", ""):
        log(f"ação desconhecida: {parsed['action']}")
        return

    STATE.status = "Entrando na sala…"
    STATE.server = parsed["server"]
    STATE.map_name = parsed["map"]

    # Se o link não trouxe servidor (site antigo), pede ao master.
    if not parsed["server"] or not parsed["token"]:
        room = request_room(parsed["origin"], parsed["map"], parsed["name"], parsed["color"])
        if room:
            parsed.update(room)

    if not parsed["server"] or not parsed["token"]:
        STATE.status = "Falha: link sem servidor/token"
        return

    try:
        LAUNCH_FILE.write_text(json.dumps(parsed, ensure_ascii=False, indent=2), encoding="utf-8")
    except OSError:
        pass

    STATE.last_launch = parsed
    STATE.status = f"Conectando em {parsed['server']}…"
    log(f"abrindo sala {parsed['server']} (mapa {parsed['map'] or '?'})")
    start_game(parsed)


def request_room(origin: str, map_id: str, name: str, color: str) -> dict | None:
    if not map_id:
        return None
    url = origin.rstrip("/") + "/api/launch"
    body = json.dumps({"map_id": map_id, "name": name, "color": color}).encode("utf-8")
    request = urllib.request.Request(url, data=body, headers={"content-type": "application/json"})
    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            data = json.loads(response.read().decode("utf-8"))
        log(f"master alocou {data.get('server')}")
        return {
            "server": data.get("server", ""),
            "token": data.get("token", ""),
            "map": data.get("map_id", map_id),
            "origin": origin,
        }
    except (OSError, ValueError) as error:
        log(f"falha ao alocar sala: {error}")
        return None


# ---------------------------------------------------------------------------
# Jogo (engine Lua)
# ---------------------------------------------------------------------------

def engine_command(launch: dict) -> list[str] | None:
    """Comando que abre o jogo. O engine LoomiaEngine recebe `chave=valor`."""
    engine = os.environ.get("LOOMIA_ENGINE") or str(APPDIR / "usr" / "bin" / "loomiaengine")
    client_dir = os.environ.get("LOOMIA_GAME_DIR") or str(APPDIR / "usr" / "share" / "loomia" / "game")
    if not Path(engine).exists():
        return None
    arguments = [
        engine, client_dir,
        f"server={launch['server']}",
        f"token={launch['token']}",
        f"map={launch.get('map', '')}",
        f"name={launch.get('name', DEFAULT_NAME)}",
        f"color={launch.get('color', 'eabe19')}",
        f"origin={launch.get('origin', DEFAULT_MASTER)}",
    ]
    return arguments


def start_game(launch: dict) -> None:
    command = engine_command(launch)
    if not command:
        STATE.status = "Engine LoomiaEngine não encontrado no AppImage"
        log("engine ausente: coloque o binário em usr/bin/loomiaengine")
        return

    if STATE.game_pid and is_alive(STATE.game_pid):
        log("fechando a sessão anterior")
        terminate(STATE.game_pid)

    try:
        STATE_DIR.mkdir(parents=True, exist_ok=True)
        output = GAME_LOG.open("a", encoding="utf-8")
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        STATE.game_pid = process.pid
        STATE.game_started_at = time.time()
        STATE.status = "No jogo"
        log(f"jogo iniciado (pid {process.pid})")
    except OSError as error:
        STATE.status = f"Falha ao abrir o jogo: {error}"
        log(f"falha ao abrir o jogo: {error}")


def is_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def terminate(pid: int) -> None:
    try:
        os.kill(pid, 15)
    except OSError:
        pass


# ---------------------------------------------------------------------------
# Transporte local para o engine (UDP)
# ---------------------------------------------------------------------------

class GameBridge:
    """Fala UDP com a sala em nome do engine.

    O engine Lua (via módulo `udp`) manda datagramas para 127.0.0.1:7788 e este
    serviço os repassa à sala, devolvendo as respostas ao mesmo cliente. Assim o
    jogo nunca abre socket próprio e o AppImage controla a sessão (fechar o
    cliente derruba a conexão).
    """

    def __init__(self, host: str = BRIDGE_HOST, port: int = BRIDGE_PORT):
        self.host = host
        self.port = port
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.socket.bind((host, port))
        self.running = True
        self.clients: dict[tuple[str, int], tuple[str, int]] = {}
        self.lock = threading.Lock()

    def serve(self) -> None:
        log(f"ponte UDP do engine em {self.host}:{self.port}")
        self.socket.settimeout(0.2)
        upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        upstream.setblocking(False)
        while self.running:
            try:
                data, address = self.socket.recvfrom(65535)
            except socket.timeout:
                data = None
            except OSError:
                break

            if data is not None:
                if data.startswith(b"{"):
                    try:
                        message = json.loads(data.decode("utf-8"))
                    except ValueError:
                        message = None
                    if isinstance(message, dict) and message.get("type") == "show":
                        COMMANDS.put("show")
                        self.socket.sendto(b"ok\n", address)
                        continue
                with self.lock:
                    target = self.clients.get(address)
                if target:
                    try:
                        upstream.sendto(data, target)
                    except OSError:
                        pass

            # Respostas da sala de volta para o engine.
            try:
                reply, source = upstream.recvfrom(65535)
            except BlockingIOError:
                continue
            except OSError:
                break
            with self.lock:
                for engine_address, server in list(self.clients.items()):
                    if server == source:
                        try:
                            self.socket.sendto(reply, engine_address)
                        except OSError:
                            pass

    def stop(self) -> None:
        self.running = False


# ---------------------------------------------------------------------------
# Bandeja do sistema
# ---------------------------------------------------------------------------

class Tray:
    def __init__(self, appimage: Path):
        self.appimage = appimage
        self.process: subprocess.Popen | None = None
        self.helper = APPDIR / "usr" / "bin" / "loomia-tray"

    def start(self) -> None:
        if os.environ.get("LOOMIA_NO_TRAY") == "1" or not self.helper.exists():
            log("bandeja desativada" if os.environ.get("LOOMIA_NO_TRAY") else "helper da bandeja ausente")
            return
        self.process = subprocess.Popen([
            str(self.helper),
            "--title", APP_NAME,
            "--tooltip", "Cliente Loomia rodando",
            "--left-click", f"{self.appimage} --show",
            "--menu", f"Abrir cliente={self.appimage} --show",
            "--menu", f"Fechar cliente={self.appimage} --quit",
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, start_new_session=True)
        log("ícone da bandeja iniciado")

    def stop(self) -> None:
        if self.process and self.process.poll() is None:
            self.process.terminate()


# ---------------------------------------------------------------------------
# Janela de fallback (Xlib puro, sem Tk)
# ---------------------------------------------------------------------------

class X11Window:
    """Janela mínima via libX11 quando o Python não tem Tk.

    Desenha o texto com a fonte padrão do servidor X (sem dependência de
    fontes extras). Minimizar = esconder; a vida útil continua no processo.
    """

    WIDTH = 460
    HEIGHT = 260

    def __init__(self):
        import ctypes
        import ctypes.util

        self.ctypes = ctypes
        self.lib = ctypes.CDLL(ctypes.util.find_library("X11") or "libX11.so.6")
        self.lib.XOpenDisplay.restype = ctypes.c_void_p
        self.lib.XOpenDisplay.argtypes = [ctypes.c_char_p]
        self.display = self.lib.XOpenDisplay(None)
        self.window = None
        if not self.display:
            return

        self.lib.XDefaultRootWindow.restype = ctypes.c_ulong
        self.lib.XDefaultRootWindow.argtypes = [ctypes.c_void_p]
        root = self.lib.XDefaultRootWindow(self.display)

        self.lib.XCreateSimpleWindow.restype = ctypes.c_ulong
        self.lib.XCreateSimpleWindow.argtypes = [ctypes.c_void_p, ctypes.c_ulong,
                                                 ctypes.c_int, ctypes.c_int, ctypes.c_uint,
                                                 ctypes.c_uint, ctypes.c_uint, ctypes.c_ulong,
                                                 ctypes.c_ulong]
        self.window = self.lib.XCreateSimpleWindow(self.display, root, 120, 120,
                                                   self.WIDTH, self.HEIGHT, 2, 0xEABE19, 0x141E30)
        self.lib.XStoreName.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_char_p]
        self.lib.XStoreName(self.display, self.window, b"Loomia")
        self.lib.XSelectInput.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_long]
        self.lib.XSelectInput(self.display, self.window, (1 << 17) | (1 << 15))  # Exposure | Visibility
        self.gc = self.lib.XDefaultGC(self.display, self.lib.XDefaultScreen(self.display))

    def _draw(self) -> None:
        if not self.window:
            return
        ctypes_ = self.ctypes
        lib = self.lib
        lib.XSetForeground.argtypes = [ctypes_.c_void_p, ctypes_.c_ulong, ctypes_.c_ulong]
        lib.XSetForeground(self.display, self.gc, 0x141E30)
        lib.XFillRectangle.argtypes = [ctypes_.c_void_p, ctypes_.c_ulong, ctypes_.c_ulong,
                                       ctypes_.c_int, ctypes_.c_int, ctypes_.c_uint, ctypes_.c_uint]
        lib.XFillRectangle(self.display, self.window, self.gc, 0, 0, self.WIDTH, self.HEIGHT)

        lib.XDrawString.argtypes = [ctypes_.c_void_p, ctypes_.c_ulong, ctypes_.c_ulong,
                                    ctypes_.c_int, ctypes_.c_int, ctypes_.c_char_p, ctypes_.c_int]

        def text(line: str, x: int, y: int, colour: int, size: int = 1) -> None:
            lib.XSetForeground(self.display, self.gc, colour)
            lib.XDrawString(self.display, self.window, self.gc, x, y,
                            line.encode("utf-8"), len(line.encode("utf-8")))

        text("Cliente Loomia Pronto", 24, 50, 0x45D5FF)
        text("Va para o site do Loomia e se divirta!", 24, 86, 0xFFFFFF)
        text(f"Status: {STATE.status}", 24, 130, 0x9FD4FF)
        text(f"Mapa: {STATE.map_name or '-'}", 24, 156, 0xCCCCCC)
        text(f"Sala: {STATE.server or '-'}", 24, 182, 0xCCCCCC)
        text("Minimize esta janela: o cliente continua na bandeja.", 24, 222, 0x8899AA)

    def show(self) -> None:
        if not self.window:
            return
        self.lib.XMapRaised.argtypes = [self.ctypes.c_void_p, self.ctypes.c_ulong]
        self.lib.XMapRaised(self.display, self.window)
        self.lib.XFlush(self.display)
        self._draw()
        self.lib.XFlush(self.display)

    def hide(self) -> None:
        if not self.window:
            return
        self.lib.XUnmapWindow.argtypes = [self.ctypes.c_void_p, self.ctypes.c_ulong]
        self.lib.XUnmapWindow(self.display, self.window)
        self.lib.XFlush(self.display)

    def pump(self) -> None:
        """Drena eventos do X sem bloquear (Expose = redesenha)."""
        if not self.display or not self.window:
            return
        if not getattr(self, "_event", None):
            self._event = (self.ctypes.c_char * 192)()  # XEvent cabe em 192 bytes
        lib = self.lib
        lib.XPending.argtypes = [self.ctypes.c_void_p]
        lib.XPending.restype = self.ctypes.c_int
        lib.XNextEvent.argtypes = [self.ctypes.c_void_p, self.ctypes.c_void_p]
        while lib.XPending(self.display) > 0:
            lib.XNextEvent(self.display, self.ctypes.byref(self._event))
            self._draw()

    def destroy(self) -> None:
        if self.display and self.window:
            self.lib.XDestroyWindow(self.display, self.window)
            self.lib.XCloseDisplay(self.display)
            self.window = None
            self.display = None


# ---------------------------------------------------------------------------
# Janela (tkinter)
# ---------------------------------------------------------------------------

class ClientWindow:
    def __init__(self, appimage: Path, on_close: callable, on_quit: callable):
        self.appimage = appimage
        self.on_close = on_close
        self.on_quit = on_quit
        self.root = None
        self.labels = {}

    def _build(self) -> None:
        try:
            import tkinter as tk
            from tkinter import ttk
        except ImportError:
            # Python sem Tk (build mínimo): cai para a janela em Xlib.
            self.fallback = X11Window()
            return

        self.root = tk.Tk()
        self.root.title(APP_NAME)
        self.root.geometry("460x320")
        self.root.minsize(420, 300)
        try:
            self.root.iconphoto(True, tk.PhotoImage(file=str(ensure_icon())))
        except Exception:
            pass  # SVG não é lido pelo Tk antigo; sem problema.
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

        style = ttk.Style(self.root)
        try:
            style.theme_use("clam")
        except Exception:
            pass

        frame = ttk.Frame(self.root, padding=20)
        frame.pack(fill="both", expand=True)

        ttk.Label(frame, text="Cliente Loomia Pronto",
                  font=("DejaVu Sans", 18, "bold")).pack(anchor="w")
        ttk.Label(frame, text="Vá para o site do Loomia e se divirta!",
                  font=("DejaVu Sans", 12)).pack(anchor="w", pady=(2, 12))

        ttk.Separator(frame).pack(fill="x", pady=8)

        self.labels["status"] = ttk.Label(frame, text="Status: Pronto", font=("DejaVu Sans", 10, "bold"))
        self.labels["status"].pack(anchor="w")
        self.labels["map"] = ttk.Label(frame, text="Mapa: —")
        self.labels["map"].pack(anchor="w", pady=(4, 0))
        self.labels["server"] = ttk.Label(frame, text="Sala: —")
        self.labels["server"].pack(anchor="w")

        ttk.Label(frame, text="Minimize esta janela: o cliente continua rodando na bandeja.",
                  wraplength=400, foreground="#555").pack(anchor="w", pady=(12, 0))

        buttons = ttk.Frame(frame)
        buttons.pack(side="bottom", fill="x", pady=(16, 0))
        ttk.Button(buttons, text="Abrir o site", command=self._open_site).pack(side="left")
        ttk.Button(buttons, text="Minimizar para a bandeja", command=self.minimize).pack(side="left", padx=8)
        ttk.Button(buttons, text="Fechar Cliente", command=self.on_quit).pack(side="right")

        self.root.after(250, self._refresh)

    def _refresh(self) -> None:
        if not self.root:
            return
        self.labels["status"].configure(text=f"Status: {STATE.status}")
        self.labels["map"].configure(text=f"Mapa: {STATE.map_name or '—'}")
        self.labels["server"].configure(text=f"Sala: {STATE.server or '—'}")
        self.root.after(250, self._refresh)

    def pump(self) -> None:
        """Drena a janela de fallback (Xlib); o Tk cuida do próprio loop."""
        fallback = getattr(self, "fallback", None)
        if fallback:
            try:
                fallback.pump()
            except Exception:
                pass

    def _open_site(self) -> None:
        origin = STATE.last_launch.get("origin") or DEFAULT_MASTER
        subprocess.run(["xdg-open", origin], check=False, capture_output=True)

    def show(self) -> None:
        if getattr(self, "fallback", None):
            self.fallback.show()
            return
        if not self.root and not getattr(self, "fallback", None):
            self._build()
        if getattr(self, "fallback", None):
            self.fallback.show()
            return
        if not self.root:
            return
        self.root.deiconify()
        self.root.lift()
        try:
            self.root.focus_force()
        except Exception:
            pass

    def minimize(self) -> None:
        if getattr(self, "fallback", None):
            self.fallback.hide()
            return
        if self.root:
            self.root.iconify()

    def run(self) -> None:
        self.show()
        self.root.mainloop()

    def destroy(self) -> None:
        if getattr(self, "fallback", None):
            self.fallback.destroy()
            return
        if self.root:
            try:
                self.root.destroy()
            except Exception:
                pass
            self.root = None


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    arguments = sys.argv[1:]
    appimage = app_path()

    register_protocol(force="--register-only" in arguments)
    if "--register-only" in arguments:
        return 0
    link = next((argument for argument in arguments if argument.lower().startswith("loomia:")), None)

    # Instância já rodando? Só mostra a janela existente; se veio um link, a
    # instância principal o recebe pelo arquivo (ver `link_queue_loop`).
    if not acquire_lock():
        if link:
            STATE_DIR.mkdir(parents=True, exist_ok=True)
            try:
                LAUNCH_FILE.write_text(json.dumps(parse_launch_link(link) or {}), encoding="utf-8")
            except OSError:
                pass
        show_existing()
        return 0
    PID_PATH.write_text(str(os.getpid()), encoding="utf-8")
    log(f"cliente iniciado (pid {os.getpid()})")

    bridge = GameBridge()
    threading.Thread(target=bridge.serve, daemon=True).start()

    # Nada pendente de execuções anteriores é reutilizado: cada sessão começa
    # pelo link que ESTA instância recebeu.
    try:
        LAUNCH_FILE.unlink()
    except OSError:
        pass

    tray = Tray(appimage)
    if "--no-tray" not in arguments and os.environ.get("LOOMIA_NO_TRAY") != "1":
        tray.start()

    quitting = {"value": False}

    def request_quit() -> None:
        if quitting["value"]:
            return
        quitting["value"] = True
        log("fechando cliente")
        if STATE.game_pid and is_alive(STATE.game_pid):
            terminate(STATE.game_pid)
        bridge.stop()
        tray.stop()
        window.destroy()

    def request_hide() -> None:
        window.minimize()

    window = ClientWindow(appimage, request_hide, request_quit)

    def link_queue_loop() -> None:
        """Recebe os links gravados por cliques seguintes no AppImage."""
        while not quitting["value"]:
            time.sleep(0.5)
            if not LAUNCH_FILE.exists():
                continue
            try:
                pending = json.loads(LAUNCH_FILE.read_text(encoding="utf-8"))
                LAUNCH_FILE.unlink()
            except (OSError, ValueError):
                continue
            if pending.get("server") and pending.get("token"):
                COMMANDS.put("launch:" + json.dumps(pending))
                COMMANDS.put("show")

    if link:
        threading.Thread(target=launch_from_link, args=(link,), daemon=True).start()
    else:
        threading.Thread(target=link_queue_loop, daemon=True).start()

    def pump_commands() -> None:
        window.pump()
        try:
            while True:
                command = COMMANDS.get_nowait()
                if command == "show":
                    window.show()
                elif command.startswith("launch:"):
                    launch = json.loads(command[len("launch:"):])
                    STATE.map_name = launch.get("map", "")
                    STATE.server = launch.get("server", "")
                    start_game(launch)
                elif command == "quit":
                    request_quit()
                    return
        except queue.Empty:
            pass
        if not quitting["value"] and window.root:
            window.root.after(200, pump_commands)

    if window.root is None:
        # Sem Tk: loop manual com a janela X11 (a bandeja/segunda instância
        # continuam funcionando via `COMMANDS`).
        while not quitting["value"]:
            pump_commands()
            time.sleep(0.1)
        return 0

    window.root.after(200, pump_commands)
    try:
        window.root.mainloop()
    except KeyboardInterrupt:
        request_quit()
    return 0


if __name__ == "__main__":
    sys.exit(main())
