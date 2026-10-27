// Shell do editor — redesign: topbar com ícones, rail de ferramentas à
// esquerda, viewport central, hierarquia + propriedades à direita, faixa de
// recursos embaixo e editor Lua integrado. Tudo deriva do tamanho real do
// framebuffer (responsivo) e das cores do tema.
#include "editor.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cmath>
#include <cstring>

namespace loomia {

namespace {

// Ícones simples desenhados com rects/triângulos (sem dependência externa).
void iconSelect(UiOverlay& ui, float x, float y, float s, const Theme& th) {
  // Cursor de seta: triângulo fino + cauda retangular.
  ui.tri(x + s*0.22f, y + s*0.12f, x + s*0.22f, y + s*0.68f, x + s*0.52f, y + s*0.5f, th.inkR, th.inkG, th.inkB);
  ui.tri(x + s*0.22f, y + s*0.12f, x + s*0.52f, y + s*0.5f, x + s*0.22f, y + s*0.5f, th.inkR, th.inkG, th.inkB);
  ui.tri(x + s*0.22f, y + s*0.55f, x + s*0.52f, y + s*0.78f, x + s*0.34f, y + s*0.78f, th.inkR, th.inkG, th.inkB);
}
void iconMove(UiOverlay& ui, float x, float y, float s, const Theme& th) {
  const float c = s/2;
  // Cruz de 4 setas com haste central limpa.
  ui.rect(x + c - s*0.04f, y + s*0.2f, s*0.08f, s*0.6f, th.inkR, th.inkG, th.inkB);
  ui.rect(x + s*0.2f, y + c - s*0.04f, s*0.6f, s*0.08f, th.inkR, th.inkG, th.inkB);
  // pontas (4 triângulos)
  ui.tri(x + c, y + s*0.08f, x + c - s*0.09f, y + s*0.24f, x + c + s*0.09f, y + s*0.24f, th.inkR, th.inkG, th.inkB);
  ui.tri(x + c, y + s*0.92f, x + c - s*0.09f, y + s*0.76f, x + c + s*0.09f, y + s*0.76f, th.inkR, th.inkG, th.inkB);
  ui.tri(x + s*0.08f, y + c, x + s*0.24f, y + c - s*0.09f, x + s*0.24f, y + c + s*0.09f, th.inkR, th.inkG, th.inkB);
  ui.tri(x + s*0.92f, y + c, x + s*0.76f, y + c - s*0.09f, x + s*0.76f, y + c + s*0.09f, th.inkR, th.inkG, th.inkB);
}
void iconScale(UiOverlay& ui, float x, float y, float s, const Theme& th) {
  // Quadrado pequeno -> quadrado maior (seta diagonal).
  ui.frame(x + s*0.15f, y + s*0.45f, s*0.3f, s*0.3f, s*0.06f, th.inkR, th.inkG, th.inkB);
  ui.frame(x + s*0.5f, y + s*0.1f, s*0.36f, s*0.36f, s*0.05f, th.inkR, th.inkG, th.inkB);
  ui.tri(x + s*0.42f, y + s*0.58f, x + s*0.52f, y + s*0.48f, x + s*0.62f, y + s*0.58f, th.inkR, th.inkG, th.inkB);
}
void iconPaint(UiOverlay& ui, float x, float y, float s, const Theme& th) {
  // Balde: corpo trapezoidal + alça.
  ui.tri(x + s*0.2f, y + s*0.75f, x + s*0.35f, y + s*0.35f, x + s*0.65f, y + s*0.35f, th.inkR, th.inkG, th.inkB);
  ui.tri(x + s*0.2f, y + s*0.75f, x + s*0.65f, y + s*0.35f, x + s*0.5f, y + s*0.75f, th.inkR, th.inkG, th.inkB);
  ui.rect(x + s*0.3f, y + s*0.22f, s*0.4f, s*0.1f, th.inkR, th.inkG, th.inkB);
}
void iconErase(UiOverlay& ui, float x, float y, float s, const Theme& th) {
  // Borracha: paralelogramo inclinado + linha de chão.
  ui.tri(x + s*0.2f, y + s*0.62f, x + s*0.45f, y + s*0.28f, x + s*0.75f, y + s*0.5f, th.inkR, th.inkG, th.inkB);
  ui.tri(x + s*0.2f, y + s*0.62f, x + s*0.75f, y + s*0.5f, x + s*0.5f, y + s*0.72f, th.inkR, th.inkG, th.inkB);
  ui.rect(x + s*0.2f, y + s*0.8f, s*0.6f, s*0.05f, th.inkR, th.inkG, th.inkB);
}
void iconAdd(UiOverlay& ui, float x, float y, float s, const Theme& th) {
  // Cubo: face superior + duas laterais.
  ui.rect(x + s*0.18f, y + s*0.42f, s*0.45f, s*0.38f, th.inkR, th.inkG, th.inkB);
  ui.rect(x + s*0.18f, y + s*0.3f, s*0.45f, s*0.12f, th.inkR*0.75f, th.inkG*0.75f, th.inkB*0.75f);
  ui.rect(x + s*0.63f, y + s*0.42f, s*0.16f, s*0.38f, th.inkR*0.6f, th.inkG*0.6f, th.inkB*0.6f);
}

// Desenha o ícone PNG do atlas (se existir); senão cai no glifo vetorial.
void drawIcon(UiOverlay& ui, const IconAtlas& icons, const char* name, float x, float y, float s, const Theme& th) {
  if (icons.ready() && icons.has(name)) {
    ui.flush();          // o ícone desenha imediatamente (shader próprio)
    icons.draw(ui, name, x, y, s);
    return;
  }
  ui.rect(x, y, s, s, th.panelAltR, th.panelAltG, th.panelAltB);  // placeholder
}

bool iconButton(UiOverlay& ui, double mx, double my, bool clicked, float x, float y, float s, bool active, const Theme& th) {
  ui.rect(x, y, s, s, active ? th.accentR : th.panelAltR, active ? th.accentG : th.panelAltG, active ? th.accentB : th.panelAltB);
  ui.frame(x, y, s, s, 1.f, th.lineR, th.lineG, th.lineB);
  return clicked && mx >= x && mx <= x + s && my >= y && my <= y + s;
}

}  // namespace

// ---- barra superior: menu Arquivo, ícones de arquivo/undo/Play/tema ----
void EditorApp::drawTopbar(int fbw, int fbh, const Theme& th) {
  const float W = static_cast<float>(fbw), H = static_cast<float>(LayoutRects{}.topbarH);
  ui_.rect(0, 0, W, H, th.panelR, th.panelG, th.panelB);
  ui_.rect(0, H - 1, W, 1, th.lineR, th.lineG, th.lineB);

  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool clicked = down && !mouseWasDown_;
  if (confirmExit_) return;  // modal é desenhado no loop

  // Menu Arquivo
  const float fx = 8, fy = 6, fw = 76, fh = 34;
  ui_.rect(fx, fy, fw, fh, fileMenu_ ? th.accentR : th.panelAltR, fileMenu_ ? th.accentG : th.panelAltG, fileMenu_ ? th.accentB : th.panelAltB);
  ui_.frame(fx, fy, fw, fh, 1, th.lineR, th.lineG, th.lineB);
  uiText("Arquivo", fx + 12, fy + 10, 13, fileMenu_ ? th.accentTextR : th.inkR, fileMenu_ ? th.accentTextG : th.inkG, fileMenu_ ? th.accentTextB : th.inkB);
  if (clicked && buttonHit(mx, my, fx, fy, fw, fh)) fileMenu_ = !fileMenu_;
  if (fileMenu_) {
    const float mw = 210;
    float my2 = fy + fh + 2;
    const char* items[] = {"Novo (Ctrl+N)", "Abrir .loom", "Salvar (Ctrl+S)", "Salvar como...", "Criar cópia...", "Renomear mapa", "Voltar ao início"};
    for (int index = 0; index < 7; ++index) {
      ui_.rect(fx, my2, mw, 26, th.panelR, th.panelG, th.panelB);
      ui_.rect(fx, my2, mw, 1, th.lineR, th.lineG, th.lineB);
      uiText(items[index], fx + 10, my2 + 6, 12, th.inkR, th.inkG, th.inkB);
      if (clicked && buttonHit(mx, my, fx, my2, mw, 26)) {
        fileMenu_ = false;
        if (index == 0) newFromTemplate(0);
        else if (index == 1) {
          FILE* pipe = popen("zenity --file-selection --title='Abrir mapa .loom' 2>/dev/null | tr -d '\\n'", "r");
          char buffer[1024] = {};
          if (pipe && fgets(buffer, sizeof(buffer), pipe)) {
            std::string chosen = buffer;
            if (!chosen.empty()) { std::string err; if (!openLoom(chosen, err)) setStatus("Erro: " + err); addRecent(chosen); }
          } else setStatus("Nenhum arquivo escolhido.");
          if (pipe) pclose(pipe);
        } else if (index == 2) {
          std::string err;
          if (!filePath_.empty()) { if (saveLoom(filePath_, err)) addRecent(filePath_); else setStatus("Erro: " + err); }
          else if (!saveWithDialog()) setStatus("Salvamento cancelado.");
        } else if (index == 3) { if (!saveWithDialog()) setStatus("Salvamento cancelado."); }
        else if (index == 4) saveCopy();
        else if (index == 5) renameMap();
        else backToWelcome();
      }
      my2 += 28;
    }
  }

  // Ícones de arquivo e edição
  float ix = fx + fw + 12;
  const float is = 30, iy = 6;
  auto next = [&ix, is]() { const float r = ix; ix += is + 6; return r; };

  // Botões com ícone PNG + rótulo pequeno abaixo (fallback: rótulo puro).
  const auto topButton = [&](const char* iconName, const char* label, float bx, float bw, bool active) {
    ui_.rect(bx, iy, bw, is + 16, active ? th.accentR : th.panelAltR, active ? th.accentG : th.panelAltG, active ? th.accentB : th.panelAltB);
    ui_.frame(bx, iy, bw, is + 16, 1, th.lineR, th.lineG, th.lineB);
    if (iconName && icons_.ready() && icons_.has(iconName)) {
      drawIcon(ui_, icons_, iconName, bx + (bw - is) / 2, iy + 2, is - 6, th);
    }
    uiText(label, bx + (bw - font_.textWidth(label, 9)) / 2, iy + is - 2, 9, active ? th.accentTextR : th.inkSoftR, active ? th.accentTextG : th.inkSoftG, active ? th.accentTextB : th.inkSoftB);
  };
  auto inButton = [&](float bx, float bw) {
    return clicked && mx >= bx && mx <= bx + bw && my >= iy && my <= iy + is + 16;
  };

  {
    const float bx = next();
    topButton(nullptr, "novo", bx, is + 14, false);
    if (inButton(bx, is + 14)) newFromTemplate(0);
  }
  {
    const float bx = next();
    topButton(nullptr, "abrir", bx, is + 14, false);
    if (inButton(bx, is + 14)) {
      FILE* pipe = popen("zenity --file-selection --title='Abrir mapa .loom' 2>/dev/null | tr -d '\n'", "r");
      char buffer[1024] = {};
      if (pipe && fgets(buffer, sizeof(buffer), pipe)) {
        std::string chosen = buffer;
        if (!chosen.empty()) { std::string err; if (!openLoom(chosen, err)) setStatus("Erro: " + err); addRecent(chosen); }
      }
      if (pipe) pclose(pipe);
    }
  }
  {
    const float bx = next();
    topButton("save", "salvar", bx, is + 14, false);
    if (inButton(bx, is + 14)) {
      std::string err;
      if (!filePath_.empty()) { if (saveLoom(filePath_, err)) addRecent(filePath_); else setStatus("Erro: " + err); }
      else if (!saveWithDialog()) setStatus("Salvamento cancelado.");
    }
  }
  {
    const float bx = next();
    topButton(nullptr, "undo", bx, is + 14, false);
    if (inButton(bx, is + 14)) undo();
  }
  {
    const float bx = next();
    topButton(nullptr, "refazer", bx, is + 14, false);
    if (inButton(bx, is + 14)) redo();
  }
  {
    const float bx = next();
    topButton(nullptr, "play", bx, is + 14, playTestOpen_);
    if (inButton(bx, is + 14)) {
      if (playTestOpen_) endTestPlay(); else playTestOpen_ = true;
    }
  }
  {
    const float bx = next();
    topButton("lua", "scripts", bx, is + 14, activeTab_ == 1);
    if (inButton(bx, is + 14)) activeTab_ = 1;
  }
  {
    const float bx = next();
    topButton("theme", prefsDark_ ? "escuro" : "claro", bx, is + 14, prefsDark_);
    if (inButton(bx, is + 14)) prefsDark_ = !prefsDark_;
  }
  ix += 8;


  // Preferências no canto direito
  {
    const float px = W - 116, py = 6;
    ui_.rect(px, py, 108, fh, prefsOpen_ ? th.accentR : th.panelAltR, prefsOpen_ ? th.accentG : th.panelAltG, prefsOpen_ ? th.accentB : th.panelAltB);
    ui_.frame(px, py, 108, fh, 1, th.lineR, th.lineG, th.lineB);
    uiText("Preferências", px + 14, py + 10, 12, prefsOpen_ ? th.accentTextR : th.inkR, prefsOpen_ ? th.accentTextG : th.inkG, prefsOpen_ ? th.accentTextB : th.inkB);
    if (clicked && buttonHit(mx, my, px, py, 108, fh)) prefsOpen_ = !prefsOpen_;
    if (prefsOpen_) drawPrefs(px, py + fh + 4);
  }
}

// ---- rail vertical de ferramentas (esquerda) ----
void EditorApp::drawToolRail(int fbw, int fbh, const Theme& th) {
  if (confirmExit_) return;
  const LayoutRects lay = layout();
  const float x = 4, y = static_cast<float>(lay.topbarH) + 4;
  const float s = 36, gap = 4;
  ui_.rect(x, y, s + 8, 6 * (s + gap) + 8, th.panelR, th.panelG, th.panelB);
  ui_.frame(x, y, s + 8, 6 * (s + gap) + 8, 1, th.lineR, th.lineG, th.lineB);

  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool clicked = down && !mouseWasDown_;

  const char* names[] = {"Select", "Add", "Move", "Scale", "Paint", "Erase"};
  // Ordem visual da referência: select, add, move, scale, paint, erase.
  const int toolOrder[6] = {1, 0, 2, 3, 4, 5};
  float ty = y + 4;
  for (int entry = 0; entry < 6; ++entry) {
    const int tool = toolOrder[entry];
    const bool active = tool_ == tool;
    const float bx = x + 4;
    bool hitBtn = iconButton(ui_, mx, my, clicked, bx, ty, s, active, th);
    const char* iconNames[] = {"add", "select", "move", "scale", "paint", "erase"};
    drawIcon(ui_, icons_, iconNames[tool], bx, ty, s, th);
    switch (tool) {
      case 1: iconSelect(ui_, bx + 4, ty + 4, s - 8, th); break;
      case 0: ui_.rect(bx + s * 0.32f, ty + 4, s * 0.36f, s * 0.36f, th.inkR, th.inkG, th.inkB); break;  // add: bloco
      case 2: iconMove(ui_, bx + 4, ty + 4, s - 8, th); break;
      case 3: iconScale(ui_, bx + 4, ty + 4, s - 8, th); break;
      case 4: iconPaint(ui_, bx + 4, ty + 4, s - 8, th); break;
      case 5: iconErase(ui_, bx + 4, ty + 4, s - 8, th); break;
      default: break;
    }
    if (hitBtn) { tool_ = tool; setStatus(std::string("Ferramenta: ") + names[tool]); }
    ty += s + gap;
  }
}

// ---- hierarquia (árvore de objetos) — painel direito superior ----
void EditorApp::drawHierarchy(int fbw, int fbh, const Theme& th) {
  if (confirmExit_) return;
  const LayoutRects lay = layout();
  const float x = static_cast<float>(fbw - lay.rightW + 2);
  const float w = static_cast<float>(lay.rightW - 4);
  const float h = static_cast<float>(lay.viewportH()) * 0.55f;
  const float y = static_cast<float>(lay.topbarH) + 2;

  ui_.rect(x, y, w, h, th.panelR, th.panelG, th.panelB);
  ui_.frame(x, y, w, h, 1, th.lineR, th.lineG, th.lineB);
  uiText("Hierarquia", x + 10, y + 8, 13, th.inkR, th.inkG, th.inkB);
  ui_.rect(x, y + 26, w, 1, th.lineR, th.lineG, th.lineB);

  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool clicked = down && !mouseWasDown_;

  float ly = y + 34;
  const int maxShow = static_cast<int>((h - 42) / 20.f);
  int shown = 0;
  for (size_t index = 0; index < world_.blocks.size() && shown < maxShow; ++index, ++shown) {
    const Block& block = world_.blocks[index];
    const bool selected = std::find(selection_.begin(), selection_.end(), static_cast<int>(index)) != selection_.end();
    if (selected) ui_.rect(x + 2, ly, w - 4, 18, th.selR * 0.25f, th.selG * 0.25f, th.selB * 0.25f);
    // ícone do objeto: quadradinho da cor
    ui_.rect(x + 10, ly + 4, 10, 10, block.color.r / 255.f, block.color.g / 255.f, block.color.b / 255.f);
    char label[64];
    std::snprintf(label, sizeof(label), "Part%zu", index + 1);
    uiText(label, x + 28, ly + 4, 11, selected ? th.inkR : th.inkSoftR, selected ? th.inkG : th.inkSoftG, selected ? th.inkB : th.inkSoftB);
    if (clicked && buttonHit(mx, my, x, ly, w, 18)) {
      selected_ = static_cast<int>(index);
      selection_.clear();
      selection_.push_back(selected_);
      setStatus("Bloco " + std::to_string(selected_) + " selecionado na hierarquia.");
    }
    ly += 20;
  }
  if (shown < static_cast<int>(world_.blocks.size())) {
    char more[64];
    std::snprintf(more, sizeof(more), "... +%zu objetos", world_.blocks.size() - static_cast<size_t>(shown));
    uiText(more, x + 10, ly + 2, 11, th.inkSoftR, th.inkSoftG, th.inkSoftB);
  }
}

// ---- propriedades do objeto selecionado (painel direito inferior) ----
void EditorApp::drawProperties(int fbw, int fbh, const Theme& th) {
  if (confirmExit_) return;
  const LayoutRects lay = layout();
  const float x = static_cast<float>(fbw - lay.rightW + 2);
  const float w = static_cast<float>(lay.rightW - 4);
  const float y = static_cast<float>(lay.topbarH) + 2 + static_cast<float>(lay.viewportH()) * 0.55f + 4;
  const float h = static_cast<float>(lay.viewportH()) * 0.45f - 8;

  ui_.rect(x, y, w, h, th.panelR, th.panelG, th.panelB);
  ui_.frame(x, y, w, h, 1, th.lineR, th.lineG, th.lineB);
  uiText("Propriedades", x + 10, y + 8, 13, th.inkR, th.inkG, th.inkB);
  ui_.rect(x, y + 26, w, 1, th.lineR, th.lineG, th.lineB);

  if (selected_ < 0 || selected_ >= static_cast<int>(world_.blocks.size())) {
    uiText("Nenhum objeto selecionado.", x + 10, y + 38, 11, th.inkSoftR, th.inkSoftG, th.inkSoftB);
    return;
  }
  Block& block = world_.blocks[static_cast<size_t>(selected_)];

  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

  char line[128];
  float ly = y + 34;
  auto row = [&](const char* label, float value, bool editable, int axis) {
    uiText(label, x + 10, ly, 11, th.inkSoftR, th.inkSoftG, th.inkSoftB);
    char valueText[32];
    std::snprintf(valueText, sizeof(valueText), "%.2f", value);
    uiText(valueText, x + 110, ly, 11, th.inkR, th.inkG, th.inkB);
    return ly;
  };
  (void)row;

  std::snprintf(line, sizeof(line), "Pos  %.1f  %.1f  %.1f", block.position.x, block.position.y, block.position.z);
  uiText(line, x + 10, ly, 11, th.inkR, th.inkG, th.inkB);
  ly += 18;
  std::snprintf(line, sizeof(line), "Tam  %.1f  %.1f  %.1f", block.size.x, block.size.y, block.size.z);
  uiText(line, x + 10, ly, 11, th.inkR, th.inkG, th.inkB);
  ly += 18;

  // Cor: 10 swatches rápidos
  const unsigned char palette[][3] = {
      {230, 120, 60}, {230, 190, 40}, {100, 160, 60}, {70, 150, 200}, {110, 80, 190},
      {230, 230, 235}, {70, 70, 80}, {190, 60, 60}, {240, 160, 200}, {60, 200, 170}};
  uiText("Cor", x + 10, ly, 11, th.inkSoftR, th.inkSoftG, th.inkSoftB);
  ly += 16;
  for (int index = 0; index < 10; ++index) {
    const float sx = x + 10 + index * 22.f;
    ui_.rect(sx, ly, 18, 18, palette[index][0] / 255.f, palette[index][1] / 255.f, palette[index][2] / 255.f);
    ui_.frame(sx, ly, 18, 18, 1, th.lineR, th.lineG, th.lineB);
    if (down && mx > sx && mx < sx + 18 && my > ly && my < ly + 18) {
      pushUndo();
      block.color = {static_cast<float>(palette[index][0]), static_cast<float>(palette[index][1]), static_cast<float>(palette[index][2])};
      renderer_.upload(world_);
      markDirty();
    }
  }
  ly += 26;

  // Material: 6 chips
  uiText("Material", x + 10, ly, 11, th.inkSoftR, th.inkSoftG, th.inkSoftB);
  ly += 16;
  const char* materials[] = {"plastic", "grass", "wood", "brick", "stone", "neon"};
  for (int index = 0; index < 6; ++index) {
    const float sx = x + 10 + (index % 3) * 74.f;
    const float sy = ly + (index / 3) * 22.f;
    const bool active = materialFromName(materials[index]) == block.material;
    ui_.rect(sx, sy, 68, 18, active ? th.accentR : th.panelAltR, active ? th.accentG : th.panelAltG, active ? th.accentB : th.panelAltB);
    ui_.frame(sx, sy, 68, 18, 1, th.lineR, th.lineG, th.lineB);
    uiText(materials[index], sx + 8, sy + 4, 10, active ? th.accentTextR : th.inkSoftR, active ? th.accentTextG : th.inkSoftG, active ? th.accentTextB : th.inkSoftB);
    if (down && mx > sx && mx < sx + 68 && my > sy && my < sy + 18) {
      pushUndo();
      block.material = materialFromName(materials[index]);
      renderer_.upload(world_);
      markDirty();
    }
  }
  ly += 48;

  // Colisão
  uiText(block.collidable ? "[x] colisão" : "[ ] colisão", x + 10, ly, 11, th.inkR, th.inkG, th.inkB);
  if (down && mx > x && mx < x + w && my > ly && my < ly + 14) {
    pushUndo();
    block.collidable = !block.collidable;
    renderer_.upload(world_);
    markDirty();
  }
}

// ---- faixa inferior: recursos (miniaturas de materiais) + coordenadas ----
void EditorApp::drawResourceStrip(int fbw, int fbh, const Theme& th) {
  if (confirmExit_) return;
  const LayoutRects lay = layout();
  const float x = static_cast<float>(lay.leftW) + 2;
  const float y = static_cast<float>(fbh - lay.bottomH);
  const float w = static_cast<float>(lay.viewportW() - 4);
  const float h = static_cast<float>(lay.bottomH - 2);

  ui_.rect(x, y, w, h, th.panelR, th.panelG, th.panelB);
  ui_.frame(x, y, w, h, 1, th.lineR, th.lineG, th.lineB);
  uiText("Recursos", x + 8, y + 6, 11, th.inkSoftR, th.inkSoftG, th.inkSoftB);

  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

  // Miniaturas: 6 materiais + cores rápidas
  const char* materials[] = {"plastic", "grass", "wood", "brick", "stone", "neon"};
  for (int index = 0; index < 6; ++index) {
    const float sx = x + 10 + index * 44.f;
    const float sy = y + 24;
    const bool active = currentMaterial_ == index;
    ui_.rect(sx, sy, 38, 38, active ? th.accentR : th.panelAltR, active ? th.accentG : th.panelAltG, active ? th.accentB : th.panelAltB);
    ui_.frame(sx, sy, 38, 38, 1, th.lineR, th.lineG, th.lineB);
    // mini bloco 3D fake: 2 retângulos
    ui_.rect(sx + 7, sy + 10, 24, 14, 0.6f, 0.62f, 0.66f);
    ui_.rect(sx + 7, sy + 24, 24, 6, 0.45f, 0.47f, 0.5f);
    uiText(materials[index], sx + 2, sy + 40, 9, th.inkSoftR, th.inkSoftG, th.inkSoftB);
    if (down && mx > sx && mx < sx + 38 && my > sy && my < sy + 38) {
      currentMaterial_ = index;
      setStatus(std::string("Material do pincel: ") + materials[index]);
    }
  }

  // Bloco atual (cor/tamanho) + coordenadas do objeto selecionado
  const float infoX = x + 300.f;
  char info[160];
  if (selected_ >= 0 && selected_ < static_cast<int>(world_.blocks.size())) {
    const Block& block = world_.blocks[static_cast<size_t>(selected_)];
    std::snprintf(info, sizeof(info),
                  "Selecionado #%d  pos(%.1f, %.1f, %.1f)  tam(%.1f, %.1f, %.1f)",
                  selected_, block.position.x, block.position.y, block.position.z, block.size.x, block.size.y, block.size.z);
  } else {
    std::snprintf(info, sizeof(info), "Nada selecionado — pincel: cor(%d,%d,%d) tam(%.1f,%.1f,%.1f)",
                  currentColor_[0], currentColor_[1], currentColor_[2], currentSize_[0], currentSize_[1], currentSize_[2]);
  }
  uiText(info, infoX, y + 10, 11, th.inkR, th.inkG, th.inkB);

  // Cor atual + swatch grande
  ui_.rect(infoX, y + 30, 26, 26, currentColor_[0] / 255.f, currentColor_[1] / 255.f, currentColor_[2] / 255.f);
  ui_.frame(infoX, y + 30, 26, 26, 1, th.lineR, th.lineG, th.lineB);
  char sizeText[64];
  std::snprintf(sizeText, sizeof(sizeText), "tam pincel %.1f x %.1f x %.1f", currentSize_[0], currentSize_[1], currentSize_[2]);
  uiText(sizeText, infoX + 36, y + 36, 11, th.inkSoftR, th.inkSoftG, th.inkSoftB);
}

// ---- moldura do viewport (fundo, grid, borda) — a cena 3D é desenhada ----
// pelo loop principal dentro dessa área.
void EditorApp::drawViewportFrame(int fbw, int fbh, const Theme& th) {
  const LayoutRects lay = layout();
  const float x = static_cast<float>(lay.viewportX());
  const float y = static_cast<float>(lay.viewportY());
  const float w = static_cast<float>(lay.viewportW());
  const float h = static_cast<float>(lay.viewportH());
  ui_.frame(x, y, w, h, 1, th.lineR, th.lineG, th.lineB);
  uiText("Viewport", x + 8, y + 6, 10, th.inkSoftR, th.inkSoftG, th.inkSoftB);
}

// ---- linha de status embaixo de tudo ----
void EditorApp::drawStatusLine(int fbw, int fbh, const Theme& th) {
  const float H = static_cast<float>(fbh);
  ui_.rect(0, H - UI_STATUS_H, static_cast<float>(fbw), static_cast<float>(UI_STATUS_H), th.panelAltR, th.panelAltG, th.panelAltB);
  ui_.rect(0, H - UI_STATUS_H, static_cast<float>(fbw), 1, th.lineR, th.lineG, th.lineB);
  char left[256];
  std::snprintf(left, sizeof(left), "%s %s  |  %zu objetos  |  %s",
                dirty_ ? "*" : " ",
                filePath_.empty() ? "(sem título)" : filePath_.c_str(),
                world_.blocks.size(), status_.c_str());
  uiText(left, 10, H - UI_STATUS_H + 6, 12, th.inkR, th.inkG, th.inkB);
}

// ---- editor Lua integrado (aba Scripts): código + console ----
void EditorApp::drawLuaEditor(int fbw, int fbh, const Theme& th) {
  // A sidebar da aba Scripts (drawScriptsPanel) mostra a lista; aqui fica o
  // console de saída na faixa inferior quando um script está aberto.
  if (luaEditorScript_ < 0 || luaEditorScript_ >= static_cast<int>(loomDoc_.scripts.size())) return;
  const LayoutRects lay = layout();
  const float x = static_cast<float>(lay.leftW) + 2;
  const float y = static_cast<float>(fbh - lay.bottomH);
  const float w = static_cast<float>(lay.viewportW() - 4);
  ui_.rect(x, y + 2, w, 40, th.panelAltR, th.panelAltG, th.panelAltB);
  uiText("console:", x + 8, y + 8, 10, th.inkSoftR, th.inkSoftG, th.inkSoftB);
  uiText(luaConsole_.empty() ? "(sem saída)" : luaConsole_, x + 60, y + 8, 10, th.inkR, th.inkG, th.inkB);
}

}  // namespace loomia
