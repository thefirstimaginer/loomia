// Loomia Studio — entry point.
//
// Uso:
//   loomia-studio               abre a tela de boas-vindas
//   loomia-studio mapa.loom     abre direto o mapa
//   loomia-studio --export X    (futuro) exportação em lote
#include "studio.h"

#include <cstdio>
#include <string>

int main(int argc, char** argv) {
  std::string openPath;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--help" || arg == "-h") {
      std::printf("Loomia Studio - editor visual de mapas .loom\n"
                  "  loomia-studio [mapa.loom]\n");
      return 0;
    }
    if (!arg.empty() && arg[0] != '-') openPath = arg;
  }
  // O caminho de linha de comando é entregue ao editor via variável de
  // ambiente interna (simples e sem estados globais expostos).
  if (!openPath.empty()) setenv("LOOMIA_STUDIO_OPEN", openPath.c_str(), 1);
  return loomia::runStudio() == loomia::EditorResult::Quit ? 0 : 0;
}
