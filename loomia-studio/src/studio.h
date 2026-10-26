// Loomia Studio — editor visual de mapas .loom.
//
// Mesma engine do cliente (GLFW + OpenGL 3.3 + WorldRenderer). A UI (menus,
// painel de propriedades, welcome screen) é desenhada em cima do mundo com
// uma barra inferior estilo IDE, sem dependência de toolkit.
#pragma once

#include <string>

#include "world.h"

namespace loomia {

enum class EditorResult { Quit, OpenAgain };

/** Roda o app inteiro (welcome -> editor). Bloqueia até o usuário sair. */
EditorResult runStudio();

}  // namespace loomia
