// Carregador de PNG mínimo (não-entrelaçado, 8-bit: RGB/RGBA/gray/gray+alpha)
// para os ícones do studio — sem dependências além do zlib do sistema.
//
// Usado por IconAtlas (editor_shell.cpp) para desenhar os ícones de
// misc/icons/loomia-studio-icons no overlay da UI.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace loomia {

struct PngImage {
  int width = 0, height = 0;
  std::vector<uint8_t> rgba;  // 4 bytes por pixel
  bool ok = false;
};

/** Decodifica um PNG da memória. Suporta: bit depth 8, color types
 *  0 (gray), 2 (RGB), 3 (palette), 4 (gray+alpha), 6 (RGBA); sem interlace. */
PngImage decodePng(const std::vector<uint8_t>& bytes, std::string& error);

PngImage loadPngFile(const std::string& path, std::string& error);

}  // namespace loomia
