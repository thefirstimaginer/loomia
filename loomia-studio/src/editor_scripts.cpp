// Editor de scripts da aba "Scripts" (lista + código inline por script).
//
// Os scripts ficam no documento v1 (loomDoc_.scripts). A edição é simples:
// selecionar, editar source em caixa grande, adicionar/remover.
#include "editor.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace loomia {

namespace {
// Caixa de texto multilinha mínima: uma string por linha, cursor = clique.
struct SimpleTextBox {
  std::string* text = nullptr;
  float x = 0, y = 0, w = 0, h = 0;
  bool active = false;
};
}  // namespace

// Sidebar da aba Scripts: lista de scripts + editor do selecionado.
void EditorApp::drawScriptsPanel(int fbw, int fbh) {
  const float x = 12.f, y = static_cast<float>(UI_BAR_H) + 44.f;
  const float w = static_cast<float>(sidebarWidth_);
  const float h = static_cast<float>(fbh) - y - UI_STATUS_H - 12.f;

  ui_.rect(x, y, w, h, 1.f, 1.f, 1.f);
  ui_.frame(x, y, w, h, 2, 0.08f, 0.12f, 0.17f);
  uiText("Scripts do mapa", x + 14, y + 12, 15, 0.08f, 0.12f, 0.17f);

  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool clicked = down && !mouseWasDown_;

  // Adicionar script
  const float addX = x + w - 96;
  ui_.rect(addX, y + 8, 84.f, 26.f, 0.91f, 0.71f, 0.17f);
  ui_.frame(addX, y + 8, 84.f, 26.f, 2, 0.08f, 0.12f, 0.17f);
  uiText("+ novo", addX + 22, y + 14, 13, 0.15f, 0.12f, 0.f);
  if (clicked && buttonHit(mx, my, addX, y + 8, 84.f, 26.f)) {
    LoomScript script;
    script.id = "script" + std::to_string(loomDoc_.scripts.size() + 1);
    script.language = "lua";
    script.target = "server";
    script.source = "-- script Lua do mapa\n";
    loomDoc_.scripts.push_back(script);
    markDirty();
  }

  // Lista
  float ly = y + 44;
  for (size_t index = 0; index < loomDoc_.scripts.size(); ++index) {
    const LoomScript& script = loomDoc_.scripts[index];
    const bool active = scriptPanelSel_ == static_cast<int>(index);
    ui_.rect(x + 10, ly, w - 20, 26.f, active ? 0.91f : 0.97f, active ? 0.71f : 0.97f, active ? 0.17f : 1.f);
    uiText(script.id + " (" + script.target + ")", x + 18, ly + 7, 12, 0.2f, 0.25f, 0.32f);
    if (clicked && buttonHit(mx, my, x + 10, ly, w - 20, 26.f)) scriptPanelSel_ = static_cast<int>(index);
    ly += 30;
  }

  // Editor do script selecionado
  if (scriptPanelSel_ >= 0 && scriptPanelSel_ < static_cast<int>(loomDoc_.scripts.size())) {
    LoomScript& script = loomDoc_.scripts[static_cast<size_t>(scriptPanelSel_)];
    ly += 8;
    uiText("Código (" + script.id + "):", x + 14, ly, 13, 0.3f, 0.35f, 0.42f);
    ly += 22;
    // Área de edição: linhas clicáveis (uma "linha" por 14px do topo da fonte).
    const float boxH = h - (ly - y) - 40;
    ui_.rect(x + 10, ly, w - 20, boxH, 0.98f, 0.98f, 1.f);
    ui_.frame(x + 10, ly, w - 20, boxH, 1, 0.5f, 0.55f, 0.6f);

    // Desenha o código linha a linha.
    float ty = ly + 8;
    size_t start = 0;
    int visible = static_cast<int>(boxH / 18.f);
    int lineIndex = 0;
    while (start <= script.source.size() && lineIndex < visible) {
      size_t end = script.source.find('\n', start);
      const std::string line = script.source.substr(start, end == std::string::npos ? std::string::npos : end - start);
      uiText(line, x + 20, ty, 12, 0.15f, 0.2f, 0.3f);
      if (end == std::string::npos) break;
      start = end + 1;
      ty += 18;
      ++lineIndex;
    }

    // Botão editar via editor externo ($EDITOR) — simples e honesto.
    const float bx = x + 14, by = y + h - 34;
    ui_.rect(bx, by, 170.f, 26.f, 0.96f, 0.96f, 0.96f);
    ui_.frame(bx, by, 170.f, 26.f, 2, 0.08f, 0.12f, 0.17f);
    uiText("editar ($EDITOR)", bx + 18, by + 6, 12, 0.3f, 0.35f, 0.42f);
    if (clicked && buttonHit(mx, my, bx, by, 170.f, 26.f)) {
      // Salva o script num temporário e abre o editor do usuário.
      const std::string tmp = "/tmp/loomia-script-" + script.id + ".lua";
      std::ofstream file(tmp);
      file << script.source;
      file.close();
      const char* editor = std::getenv("EDITOR");
      std::string command = (editor ? editor : "nano");
      command += " " + tmp;
      if (std::system(command.c_str()) == 0) {
        std::ifstream back(tmp);
        std::stringstream buffer;
        buffer << back.rdbuf();
        script.source = buffer.str();
        markDirty();
      }
    }

    // Remover
    const float rx = x + w - 110;
    ui_.rect(rx, by, 96.f, 26.f, 0.96f, 0.9f, 0.9f);
    ui_.frame(rx, by, 96.f, 26.f, 2, 0.6f, 0.2f, 0.2f);
    uiText("remover", rx + 20, by + 6, 12, 0.65f, 0.15f, 0.15f);
    if (clicked && buttonHit(mx, my, rx, by, 96.f, 26.f)) {
      loomDoc_.scripts.erase(loomDoc_.scripts.begin() + scriptPanelSel_);
      scriptPanelSel_ = -1;
      markDirty();
    }
  }
}

// Sidebar da aba Cena: contagem, materiais do mapa e dicas rápidas.
void EditorApp::drawScenePanel(int fbw, int fbh) {
  const float x = 12.f, y = static_cast<float>(UI_BAR_H) + 44.f;
  const float w = static_cast<float>(sidebarWidth_);
  const float h = static_cast<float>(fbh) - y - UI_STATUS_H - 12.f;

  ui_.rect(x, y, w, h, 1.f, 1.f, 1.f);
  ui_.frame(x, y, w, h, 2, 0.08f, 0.12f, 0.17f);
  uiText("Cena", x + 14, y + 12, 15, 0.08f, 0.12f, 0.17f);

  char info[128];
  std::snprintf(info, sizeof(info), "%zu objetos", world_.blocks.size());
  uiText(info, x + 14, y + 40, 13, 0.3f, 0.35f, 0.42f);

  // Contagem por material.
  int counts[6] = {};
  for (const Block& block : world_.blocks) counts[static_cast<int>(block.material)]++;
  const char* names[] = {"plastic", "grass", "wood", "brick", "stone", "neon"};
  float ly = y + 66;
  for (int index = 0; index < 6; ++index) {
    if (counts[index] == 0) continue;
    char line[96];
    std::snprintf(line, sizeof(line), "%s: %d", names[index], counts[index]);
    uiText(line, x + 24, ly, 12, 0.35f, 0.4f, 0.46f);
    ly += 20;
  }

  uiText("Dicas", x + 14, ly + 10, 14, 0.08f, 0.12f, 0.17f);
  const char* tips[] = {
      "1-6 trocam a ferramenta",
      "Ctrl+S salva em .loom v1",
      "Ctrl+D duplica a seleção",
      "G arredonda posições",
      "F foca a câmera no bloco",
      "C abre a cor do céu"};
  float ty = ly + 34;
  for (const char* tip : tips) {
    uiText(tip, x + 24, ty, 12, 0.45f, 0.5f, 0.56f);
    ty += 20;
  }
}

}  // namespace loomia
