#!/bin/bash
# build.sh — empacota o cliente novo (SFML + OpenGL 3.3 + GLFW) num AppImage.
#
# Uso:   ./build.sh
# Saída: ../../dist/Loomia-x86_64.AppImage
#
# O AppImage:
#   - registra `loomia://` na primeira execução (uma única vez);
#   - abre o launcher SFML na primeira execução;
#   - carrega o SFML vendorizado de dentro do pacote (roda sem instalar nada).
set -e
cd "$(dirname "$0")"

ROOT="$(cd ../.. && pwd)"
APPDIR="$(pwd)/AppDir"
DIST="$ROOT/dist"
BUILD="$ROOT/loomia-client/build-appimage"
LOG="$BUILD"
mkdir -p "$LOG"

log() { echo "[appimage] $*"; }

# ---------------------------------------------------------------------------
# 1. Compilar o cliente (binário + SFML vendorizado)
# ---------------------------------------------------------------------------
log "compilando o loomia-client (default: exige login)"
cmake -S "$ROOT/loomia-client" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
  -DLOOMIA_DEBUG_BUILD=OFF \
  > "$LOG/cmake.log" 2>&1 || { tail -25 "$LOG/cmake.log" >&2; exit 1; }
cmake --build "$BUILD" -j"$(nproc)" > "$LOG/build.log" 2>&1 \
  || { grep -m 12 -E "error:|Erro" "$LOG/build.log" >&2; exit 1; }

CLIENT_BIN="$(find "$BUILD" -maxdepth 1 -type f -name 'loomia-client' -executable | head -1)"
[ -n "$CLIENT_BIN" ] || { echo "[appimage] ERRO: binário não encontrado" >&2; exit 1; }
log "binário: $CLIENT_BIN"

# ---------------------------------------------------------------------------
# 2. Montar o AppDir
# ---------------------------------------------------------------------------
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib/loomia" "$APPDIR/usr/share/icons/hicolor/256x256/apps" \
         "$APPDIR/usr/share/applications" "$APPDIR/usr/share/metainfo"

cp "$CLIENT_BIN" "$APPDIR/usr/bin/loomia-client"
cp "$ROOT/loomia-client/appimage/AppRun" "$APPDIR/AppRun"
chmod +x "$APPDIR/AppRun"
cp "$ROOT/loomia-client/appimage/loomia.desktop" "$APPDIR/loomia.desktop"
cp "$ROOT/loomia-client/appimage/loomia.desktop" "$APPDIR/usr/share/applications/loomia.desktop"

# SFML vendorizado (as .so que o binário linka).
SFML_LIB_DIR="$ROOT/new-engine-src/SFML/lib"
for lib in libsfml-graphics libsfml-window libsfml-system; do
  found="$(ls "$SFML_LIB_DIR"/$lib.so* 2>/dev/null | head -1)"
  if [ -n "$found" ]; then
    cp -L "$found" "$APPDIR/usr/lib/loomia/" || true
    # também copia as major-version links (libsfml-x.so.3 etc.)
    for link in $(ls "$SFML_LIB_DIR" | grep "^$lib\.so\." || true); do
      cp -L "$SFML_LIB_DIR/$link" "$APPDIR/usr/lib/loomia/" 2>/dev/null || true
    done
  else
    # usa as do sistema
    for f in $(ldd "$CLIENT_BIN" | awk '/sfml/{print $3}'); do
      cp -L "$f" "$APPDIR/usr/lib/loomia/" 2>/dev/null || true
    done
  fi
done

# Ícone (se existir um .png em appimage, usa; senão gera um placeholder).
if [ -f loomia.png ]; then
  cp loomia.png "$APPDIR/loomia.png"
  cp loomia.png "$APPDIR/usr/share/icons/hicolor/256x256/apps/loomia.png"
else
  python3 - <<'PY' 2>/dev/null || true
import struct, zlib
w = h = 256
def chunk(t, d):
    return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
rows = b''.join(b'\x00' + bytes([90, 60, 170, 255]) * w for _ in range(h))
png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
       + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
open('loomia.png', 'wb').write(png)
PY
  if [ -f loomia.png ]; then
    cp loomia.png "$APPDIR/loomia.png"
    cp loomia.png "$APPDIR/usr/share/icons/hicolor/256x256/apps/loomia.png"
  fi
fi

# ---------------------------------------------------------------------------
# 3. appimagetool
# ---------------------------------------------------------------------------
TOOLS="$BUILD/tools"
mkdir -p "$TOOLS"
APPIMAGETOOL="$TOOLS/appimagetool"
if [ ! -x "$APPIMAGETOOL" ]; then
  log "baixando appimagetool"
  curl -fsSL -o "$APPIMAGETOOL" \
    "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
  chmod +x "$APPIMAGETOOL"
fi

mkdir -p "$DIST"
OUTPUT="$DIST/Loomia-x86_64.AppImage"
rm -f "$OUTPUT"
ARCH=x86_64 "$APPIMAGETOOL" --appimage-extract-and-run "$APPDIR" "$OUTPUT" 2>/dev/null \
  || ARCH=x86_64 "$APPIMAGETOOL" "$APPDIR" "$OUTPUT"
chmod +x "$OUTPUT"
log "pronto: $OUTPUT"

# ---------------------------------------------------------------------------
# 4. Build de debug (loomia-client-debug): sem login, HUD de rede.
# ---------------------------------------------------------------------------
if [ "${LOOMIA_BUILD_DEBUG:-1}" = "1" ]; then
  log "compilando o loomia-client-debug (sem login + HUD)"
  cmake -S "$ROOT/loomia-client" -B "$BUILD-debug" -DCMAKE_BUILD_TYPE=Release \
    -DLOOMIA_DEBUG_BUILD=ON > "$LOG/cmake-debug.log" 2>&1 \
    || { tail -25 "$LOG/cmake-debug.log" >&2; exit 1; }
  cmake --build "$BUILD-debug" -j"$(nproc)" > "$LOG/build-debug.log" 2>&1 \
    || { grep -m 12 -E "error:|Erro" "$LOG/build-debug.log" >&2; exit 1; }
  DEBUG_BIN="$(find "$BUILD-debug" -maxdepth 1 -type f -name 'loomia-client-debug' -executable | head -1)"
  [ -n "$DEBUG_BIN" ] && cp "$DEBUG_BIN" "$DIST/"
  log "pronto: $DIST/loomia-client-debug"
fi
