#!/bin/bash
# Monta o AppImage do cliente Loomia.
#
#   loomia-client-lovr/appimage/build.sh [--no-engine] [--engine <binário>]
#
# Pré-requisitos: cmake, compilador C/C++, python3 e as bibliotecas de
# desenvolvimento usadas pelo engine (X11/Vulkan — veja loomia-engine/README.md).
#
# Resultado: dist/Loomia-x86_64.AppImage
#
# Estrutura dentro do AppImage:
#   AppRun                        -> appimage/AppRun
#   usr/bin/loomiaengine          -> engine (LoomiaEngine)
#   usr/bin/loomia-tray           -> ícone da bandeja (C/X11)
#   usr/lib/loomia/loomia-client.py
#   usr/share/loomia/game/        -> cliente Lua (main.lua, src/, conf.lua)

set -euo pipefail

APPIMAGE_SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT="$(cd "$APPIMAGE_SRC/../.." && pwd)"
GAME_DIR="$PROJECT/loomia-client-lovr"
ENGINE_DIR="$PROJECT/loomia-engine"
DIST="$PROJECT/dist"
BUILD="$PROJECT/build/appimage"
APPDIR="$BUILD/Loomia.AppDir"
TOOLS="$BUILD/tools"

WITH_ENGINE=1
ENGINE_SOURCE=""
while [ $# -gt 0 ]; do
  case "$1" in
    --no-engine) WITH_ENGINE=0; shift ;;
    --engine) ENGINE_SOURCE="$2"; shift 2 ;;
    *) echo "opção desconhecida: $1" >&2; exit 2 ;;
  esac
done

log() { printf '\033[1;36m[appimage]\033[0m %s\n' "$*"; }

rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib/loomia" "$APPDIR/usr/share/loomia/game" "$DIST" "$TOOLS"

# --- 1. Cliente Lua + controlador -----------------------------------------
log "copiando o cliente Lua"
rsync -a --delete \
  --exclude README.md --exclude tests --exclude appimage --exclude '*.log' \
  "$GAME_DIR/" "$APPDIR/usr/share/loomia/game/"

install -m 755 "$APPIMAGE_SRC/loomia-client.py" "$APPDIR/usr/lib/loomia/loomia-client.py"
install -m 755 "$APPIMAGE_SRC/AppRun" "$APPDIR/AppRun"
install -m 644 "$APPIMAGE_SRC/loomia.desktop" "$APPDIR/loomia.desktop"
mkdir -p "$APPDIR/usr/share/applications"
install -m 644 "$APPIMAGE_SRC/loomia.desktop" "$APPDIR/usr/share/applications/loomia.desktop"

# `loomia-client` no PATH: o .desktop (registrado pelo próprio AppImage) aponta
# para este nome curto e estável, não para o caminho móvel do AppImage.
cat > "$APPDIR/usr/bin/loomia-client" <<'LAUNCHER'
#!/bin/sh
# Abre o cliente Loomia (AppImage) repassando o link loomia://.
exec "${LOOMIA_CLIENT_BIN:-@APPDIR@/AppRun}" "$@"
LAUNCHER
sed -i "s|@APPDIR@|$APPDIR|g" "$APPDIR/usr/bin/loomia-client"
chmod 755 "$APPDIR/usr/bin/loomia-client"

# --- 2. Ícone -------------------------------------------------------------
python3 - "$APPDIR" <<'PY'
import sys, re
from pathlib import Path
appdir = Path(sys.argv[1])
source = (appdir / "usr/lib/loomia/loomia-client.py").read_text(encoding="utf-8")
svg = re.search(r'ICON_SVG = """(.*?)"""', source, re.S).group(1)
(appdir / "loomia.svg").write_text(svg, encoding="utf-8")
icons = appdir / "usr/share/icons/hicolor/scalable/apps"
icons.mkdir(parents=True, exist_ok=True)
(icons / "loomia.svg").write_text(svg, encoding="utf-8")
PY

# --- 3. Ícone da bandeja (C/X11 puro, sem GTK) ----------------------------
log "compilando a bandeja (loomia-tray)"
cc -O2 -Wall -Wextra -o "$APPDIR/usr/bin/loomia-tray" "$APPIMAGE_SRC/tray/loomia-tray.c" -lX11

# --- 4. Engine ------------------------------------------------------------
if [ -z "$ENGINE_SOURCE" ] && [ "$WITH_ENGINE" = "1" ]; then
  log "compilando o LoomiaEngine"
  cmake -S "$ENGINE_DIR" -B "$ENGINE_DIR/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DLOVR_USE_VULKAN=OFF \
    -DLOVR_USE_STEAM_AUDIO=OFF \
    -DLOVR_ENABLE_HEADSET=OFF \
    -DLOVR_ENABLE_PHYSICS=OFF >/dev/null
  cmake --build "$ENGINE_DIR/build" -j"$(nproc)" >/dev/null
  ENGINE_SOURCE="$(find "$ENGINE_DIR/build" -maxdepth 2 -type f -name 'loomiaengine*' | head -1)"
fi

if [ -n "$ENGINE_SOURCE" ] && [ -x "$ENGINE_SOURCE" ]; then
  log "engine: $ENGINE_SOURCE"
  cp "$ENGINE_SOURCE" "$APPDIR/usr/bin/loomiaengine"
  chmod +x "$APPDIR/usr/bin/loomiaengine"
else
  log "AVISO: engine não encontrado — o AppImage abre a janela, mas não inicia o jogo"
fi

# --- 5. appimagetool ------------------------------------------------------
APPIMAGETOOL="$TOOLS/appimagetool"
if [ ! -x "$APPIMAGETOOL" ]; then
  log "baixando appimagetool"
  curl -fsSL -o "$APPIMAGETOOL" \
    "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
  chmod +x "$APPIMAGETOOL"
fi

OUTPUT="$DIST/Loomia-x86_64.AppImage"
rm -f "$OUTPUT"
ARCH=x86_64 "$APPIMAGETOOL" --appimage-extract-and-run "$APPDIR" "$OUTPUT" 2>/dev/null \
  || ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "$OUTPUT"

chmod +x "$OUTPUT"
log "pronto: $OUTPUT"
