// Autosave, histórico de alterações e navegador de assets do projeto.
//
// Autosave: grava em ~/.loomia-studio/autosave/<id>.loom a cada 90s se houver
// mudanças; o arquivo original nunca é sobrescrito pelo autosave.
// Histórico: lista de eventos (abrir/salvar/editar) com horário — útil para
// "o que mudei desde o último save".
// Assets: pastas do projeto (modelos/materiais/sons) + exemplos incluídos.
#include "editor.h"

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
  const float h = static_cast<float>(lay.bottomH - UI_STATUS_H - 2);

  ui_.rect(x, y, w, h, th.panelR, th.panelG, th.panelB);
  ui_.frame(x, y, w, h, 1, th.lineR, th.lineG, th.lineB);
  uiText("Recursos", x + 8, y + 4, 11, th.inkR, th.inkG, th.inkB);
  uiText("projeto + exemplos", x + 62, y + 5, 9, th.inkSoftR, th.inkSoftG, th.inkSoftB);

  double mx, my;
  mx = static_cast<double>(sf::Mouse::getPosition(*window_).x);
my = static_cast<double>(sf::Mouse::getPosition(*window_).y);
  const bool down = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);

  // Categorias: primitivas (projeto) e exemplos incluídos.
  struct Asset {
    const char* label;   // nome curto (cabe no card)
    const char* group;   // P = projeto, E = exemplo incluído
    int primitive;       // -1 = luz
  };
  const Asset assets[] = {
      {"Caixa", "P", 0}, {"Esfera", "P", 1}, {"Cilindro", "P", 2}, {"Luz", "P", -1},
      {"Plataforma", "E", 0}, {"Pilar", "E", 2},
  };

  float ax = x + 10;
  for (const Asset& asset : assets) {
    const float cardW = 92, cardH = h - 26;
    const bool isProject = asset.group[0] == 'P';
    // Fundo do card: tom do painel alternativo + borda — visível nos 2 temas.
    ui_.rect(ax, y + 20, cardW, cardH, th.panelAltR, th.panelAltG, th.panelAltB);
    ui_.frame(ax, y + 20, cardW, cardH, 1, th.lineR, th.lineG, th.lineB);
    // Hover: realce discreto
    if (mx > ax && mx < ax + cardW && my > y + 20 && my < y + 20 + cardH) {
      ui_.rect(ax, y + 20, cardW, cardH, 1.f, 1.f, 1.f, 0.08f);
    }

    // miniatura (quadro) no topo do card
    const float cr = asset.primitive == 1 ? 0.42f : isProject ? 0.56f : 0.36f;
    // miniatura: "bloco 3D" com cores FORTES por tipo (visível nos 2 temas)
    float fr = 0.55f, fg = 0.62f, fb = 0.72f;                              // padrão: cinza-azulado
    if (asset.primitive == 1) { fr = 0.35f; fg = 0.62f; fb = 0.92f; }      // esfera: azul
    else if (asset.primitive == 2) { fr = 0.42f; fg = 0.78f; fb = 0.45f; }  // cilindro: verde
    else if (asset.primitive < 0) { fr = 0.98f; fg = 0.85f; fb = 0.30f; }   // luz: amarelo
    else if (!isProject) { fr = 0.85f; fg = 0.55f; fb = 0.32f; }            // exemplo: laranja
    const float mx0 = ax + 10, my0 = y + 26, mw = cardW - 20, mh = 34;
    ui_.rect(mx0, my0, mw, mh - 8, fr * 0.72f, fg * 0.72f, fb * 0.72f);     // lateral
    ui_.rect(mx0, my0, mw - 7, mh - 12, fr, fg, fb);                        // frente
    ui_.rect(mx0, my0, mw - 7, 8, fr * 1.18f, fg * 1.14f, fb * 1.10f);      // topo
    ui_.frame(mx0, my0, mw, mh, 1, th.inkR, th.inkG, th.inkB);              // contorno
    (void)cr;

    // nome e origem em LINHAS DIFERENTES (sem sobreposição)
    uiText(asset.label, ax + 9, y + 68, 10, th.inkR, th.inkG, th.inkB);
    uiText(isProject ? "projeto" : "exemplo", ax + 9, y + 82, 8, th.inkSoftR, th.inkSoftG, th.inkSoftB);

    // clique: insere na cena
    if (down && mx > ax && mx < ax + cardW && my > y + 20 && my < y + 20 + cardH) {
      if (asset.primitive >= 0) addObjectPrimitive(asset.primitive);
      else addLight();
    }
    ax += cardW + 8;
  }

  // Histórico de alterações: bloco próprio com separador vertical.
  const float hx = x + w * 0.55f;
  if (hx + 120 < x + w) {
    ui_.rect(hx - 10, y + 18, 1, h - 24, th.lineR, th.lineG, th.lineB);  // divisória
    uiText("Histórico", hx, y + 24, 10, th.inkR, th.inkG, th.inkB);
    float ty = y + 40;
    int shown = 0;
    const int maxLines = static_cast<int>((h - 48) / 14.f);
    for (auto it = history_.rbegin(); it != history_.rend() && shown < maxLines; ++it, ++shown) {
      std::string line = *it;
      if (line.size() > 30) line = line.substr(0, 29) + ".";  // cabe na faixa
      uiText(line, hx, ty, 9, th.inkSoftR, th.inkSoftG, th.inkSoftB);
      ty += 14;
    }
    if (history_.empty()) uiText("(sem alterações)", hx, y + 40, 9, th.inkSoftR, th.inkSoftG, th.inkSoftB);
  }
}

}  // namespace loomia
