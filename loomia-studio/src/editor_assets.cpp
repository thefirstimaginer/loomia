// Autosave, histórico de alterações e navegador de assets do projeto.
//
// Autosave: grava em ~/.loomia-studio/autosave/<id>.loom a cada 90s se houver
// mudanças; o arquivo original nunca é sobrescrito pelo autosave.
// Histórico: lista de eventos (abrir/salvar/editar) com horário — útil para
// "o que mudei desde o último save".
// Assets: pastas do projeto (modelos/materiais/sons) + exemplos incluídos.
#include "editor.h"

#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace loomia {

namespace {

std::string autosaveDir() {
  const char* home = std::getenv("HOME");
  return (home ? std::string(home) : std::string("/tmp")) + "/.loomia-studio/autosave";
}

std::string nowStamp() {
  const std::time_t t = std::time(nullptr);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%H:%M:%S", std::localtime(&t));
  return buffer;
}

}  // namespace

// Autosave periódico (chama por frame; usa o relógio interno).
void EditorApp::tickAutosave(float dt) {
  if (!inEditor_ || !dirty_ || filePath_.empty()) return;
  autosaveTimer_ += dt;
  if (autosaveTimer_ < 90.f) return;
  autosaveTimer_ = 0.f;

  std::filesystem::create_directories(autosaveDir());
  LoomDocument doc = worldToLoom(world_);
  const std::string name = doc.id.empty() ? "sem-nome" : doc.id;
  const std::string path = autosaveDir() + "/" + name + ".loom";
  std::string error;
  if (saveLoomV1(path, doc, error)) {
    addHistory("autosave em " + path);
  } else {
    addHistory("autosave falhou: " + error);
  }
}

// Histórico de alterações (memória; aparece no painel inferior).
void EditorApp::addHistory(const std::string& entry) {
  history_.push_back("[" + nowStamp() + "] " + entry);
  if (history_.size() > 50) history_.erase(history_.begin());
}

// ===== navegador de assets =====
void EditorApp::drawAssetBrowser(int fbw, int fbh, const Theme& th) {
  const LayoutRects lay = layout();
  const float x = static_cast<float>(lay.leftW) + 2;
  const float y = static_cast<float>(fbh - lay.bottomH);
  const float w = static_cast<float>(lay.viewportW() - 4);
  const float h = static_cast<float>(lay.bottomH - 2);

  ui_.rect(x, y, w, h, th.panelR, th.panelG, th.panelB);
  ui_.frame(x, y, w, h, 1, th.lineR, th.lineG, th.lineB);
  uiText("Recursos — [P]rojeto  [E]xemplos", x + 8, y + 6, 10, th.inkSoftR, th.inkSoftG, th.inkSoftB);

  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

  // Categorias: primitivas (projeto) e exemplos incluídos.
  struct Asset {
    const char* label;
    const char* group;   // P = projeto (gerado), E = exemplo incluído
    int primitive;       // -1 = não inserível como primitiva
  };
  const Asset assets[] = {
      {"Caixa", "P", 0}, {"Esfera", "P", 1}, {"Cilindro", "P", 2}, {"Luz", "P", -1},
      {"Exemplo: plataforma", "E", 0}, {"Exemplo: pilar", "E", 2},
  };

  float ax = x + 10;
  for (const Asset& asset : assets) {
    const float cardW = 86, cardH = 52;
    const bool isProject = asset.group[0] == 'P';
    ui_.rect(ax, y + 22, cardW, cardH, isProject ? th.panelAltR : th.panelR, isProject ? th.panelAltG : th.panelG,
             isProject ? th.panelAltB : th.panelB);
    ui_.frame(ax, y + 22, cardW, cardH, 1, th.lineR, th.lineG, th.lineB);
    // miniatura: quad colorido representativo
    const float cr = asset.primitive == 1 ? 0.4f : isProject ? 0.55f : 0.35f;
    ui_.rect(ax + cardW / 2 - 10, y + 30, 20, 14, cr, cr + 0.05f, cr + 0.1f);
    uiText(asset.label, ax + 6, y + 50, 9, th.inkR, th.inkG, th.inkB);
    uiText(isProject ? "projeto" : "exemplo", ax + 6, y + 62, 8, th.inkSoftR, th.inkSoftG, th.inkSoftB);
    // clique: insere na cena
    if (down && mx > ax && mx < ax + cardW && my > y + 22 && my < y + 22 + cardH) {
      if (asset.primitive >= 0) addObjectPrimitive(asset.primitive);
      else if (std::strcmp(asset.label, "Luz") == 0) addLight();
    }
    ax += cardW + 8;
  }

  // Histórico de alterações (à direita da faixa)
  const float hx = ax + 14;
  uiText("Histórico:", hx, y + 24, 10, th.inkR, th.inkG, th.inkB);
  float ty = y + 40;
  int shown = 0;
  for (auto it = history_.rbegin(); it != history_.rend() && shown < 3; ++it, ++shown) {
    uiText(*it, hx, ty, 9, th.inkSoftR, th.inkSoftG, th.inkSoftB);
    ty += 14;
  }
}

}  // namespace loomia
