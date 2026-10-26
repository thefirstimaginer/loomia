#!/usr/bin/env bash
# Empacota o Loomia Studio como .AppImage.
#
# Uso: ./appimage/build.sh   (na raiz do loomia-studio, após compilar em build/)
set -euo pipefail

cd "$(dirname "$0")/.."
STUDIO_ROOT="$(pwd)"
BUILD="${STUDIO_ROOT}/build"
BINARY="${BUILD}/loomia-studio"
STAGE="${BUILD}/AppDir"

[[ -x "${BINARY}" ]] || { echo "compile antes: cmake -B build && cmake --build build"; exit 1; }
command -v linuxdeploy >/dev/null 2>&1 || { echo "instale linuxdeploy (https://github.com)"; exit 1; }

rm -rf "${STAGE}"
# Cria toda a estrutura necessária, incluindo a pasta padrão para o arquivo .desktop
mkdir -p "${STAGE}/usr/bin" \
         "${STAGE}/usr/share/icons/hicolor/256x256/apps" \
         "${STAGE}/usr/share/applications"

cp "${BINARY}" "${STAGE}/usr/bin/loomia-studio"

# Criando o .desktop no local padrão correto do sistema
cat > "${STAGE}/usr/share/applications/loomia-studio.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Loomia Studio
Comment=Editor visual de mapas .loom
Exec=loomia-studio
Icon=loomia-studio
Categories=Development;
DESKTOP

# Caminhos dos ícones
ICON_SRC="${STUDIO_ROOT}/../misc/icons/loomiaStudio.png"
ICON_DEST="${STAGE}/usr/share/icons/hicolor/256x256/apps/loomia-studio.png"

# Processa e padroniza o ícone para 256x256
if [[ -f "${ICON_SRC}" ]]; then
    if command -v convert >/dev/null 2>&1; then
        convert "${ICON_SRC}" -resize 256x256! "${ICON_DEST}"
    else
        cp "${ICON_SRC}" "${ICON_DEST}"
    fi
fi

# Copia o ícone também para a raiz do AppDir (exigência do appimagetool para o ambiente de execução)
cp "${ICON_DEST}" "${STAGE}/loomia-studio.png"

# Executa o linuxdeploy passando o desktop file correto. 
# Ele mesmo cuidará de criar os links necessários na raiz agora.
linuxdeploy --appdir "${STAGE}" \
            --desktop-file "${STAGE}/usr/share/applications/loomia-studio.desktop" \
            --icon-file "${ICON_DEST}" \
            --output appimage

mkdir -p dist
mv Loomia_Studio*.AppImage dist/ 2>/dev/null || mv loomia-studio*.AppImage dist/ 2>/dev/null || true
echo "AppImage gerado com sucesso na pasta dist/"
