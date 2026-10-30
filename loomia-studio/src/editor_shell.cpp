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

  // ===== botões da topbar: UMA tabela define posição, largura, ícone e ação =====
  struct TopSlot {
    const char* icon;   // PNG do atlas (nullptr = sem ícone)
    const char* label;  // rótulo pequeno
    float width;
    int action;
    bool active;
  };
  const TopSlot slots[] = {
      {nullptr, "novo", 42, 0, false},
      {nullptr, "abrir", 42, 1, false},
      {"save", "salvar", 46, 2, false},
      {nullptr, "undo", 40, 3, false},
      {nullptr, "redo", 40, 4, false},
      {nullptr, "play", 42, 5, playTestOpen_},
      {nullptr, "pausar", 48, 6, testPaused_},
      {nullptr, "reiniciar", 56, 7, false},
      {"lua", "scripts", 56, 8, activeTab_ == 1},
      {"theme", prefsDark_ ? "escuro" : "claro", 52, 9, prefsDark_},
  };

  const float is = 30, iy = 6;
  float sx = fx + fw + 12;
  for (const TopSlot& slot : slots) {
    // Fundo + borda
    ui_.rect(sx, iy, slot.width, is + 16, slot.active ? th.accentR : th.panelAltR,
             slot.active ? th.accentG : th.panelAltG, slot.active ? th.accentB : th.panelAltB);
    ui_.frame(sx, iy, slot.width, is + 16, 1, th.lineR, th.lineG, th.lineB);
    // Rótulo (com ícone ocupa a parte de baixo; sem ícone fica centralizado)
    const float labelY = slot.icon ? iy + is - 4 : iy + 11;
    uiText(slot.label, sx + (slot.width - font_.textWidth(slot.label, 9)) / 2, labelY, 9,
           slot.active ? th.accentTextR : th.inkSoftR, slot.active ? th.accentTextG : th.inkSoftG,
           slot.active ? th.accentTextB : th.inkSoftB);
    // Clique
    if (clicked && mx >= sx && mx <= sx + slot.width && my >= iy && my <= iy + is + 16) {
      switch (slot.action) {
        case 0: newFromTemplate(0); break;
        case 1: {
          FILE* pipe = popen("zenity --file-selection --title='Abrir mapa .loom' 2>/dev/null | tr -d '\\n'", "r");
          char buffer[1024] = {};
          if (pipe && fgets(buffer, sizeof(buffer), pipe)) {
            std::string chosen = buffer;
            if (!chosen.empty()) { std::string err; if (!openLoom(chosen, err)) setStatus("Erro: " + err); addRecent(chosen); }
          }
          if (pipe) pclose(pipe);
          break;
        }
        case 2: {
          std::string err;
          if (!filePath_.empty()) { if (saveLoom(filePath_, err)) addRecent(filePath_); else setStatus("Erro: " + err); }
          else if (!saveWithDialog()) setStatus("Salvamento cancelado.");
          break;
        }
        case 3: undo(); break;
        case 4: redo(); break;
        case 5:
          if (playTestOpen_) endTestPlay();
          else {
            testPlayActive_ = false;
            testPaused_ = false;
            luaConsole_.clear();
            addHistory("teste iniciado");
            playTestOpen_ = true;
          }
          break;
        case 6: if (testPaused_) resumeTestPlay(); else pauseTestPlay(); break;
        case 7: restartTestPlay(); break;
        case 8: activeTab_ = 1; break;
        case 9: prefsDark_ = !prefsDark_; break;
        default: break;
      }
    }
    sx += slot.width + 6;
  }

  // Preferências (canto direito): fundo + rótulo; ícone na 2ª passada.
  {
    const float px = W - 112, py = 6;
    ui_.rect(px, py, 104, is + 16, prefsOpen_ ? th.accentR : th.panelAltR,
             prefsOpen_ ? th.accentG : th.panelAltG, prefsOpen_ ? th.accentB : th.panelAltB);
    ui_.frame(px, py, 104, is + 16, 1, th.lineR, th.lineG, th.lineB);
    uiText("prefs", px + (104 - font_.textWidth("prefs", 9)) / 2, py + 26, 9,
           prefsOpen_ ? th.accentTextR : th.inkSoftR, prefsOpen_ ? th.accentTextG : th.inkSoftG,
           prefsOpen_ ? th.accentTextB : th.inkSoftB);
    if (clicked && buttonHit(mx, my, px, py, 104, is + 16)) prefsOpen_ = !prefsOpen_;
  }

  // ===== 2ª passada: ícones PNG por cima dos fundos (posições da tabela) =====
  if (icons_.ready()) {
    float ix2 = fx + fw + 12;
    for (const TopSlot& slot : slots) {
      if (slot.icon && icons_.has(slot.icon)) {
        ui_.flush();
        const float iconSize = 17;
        icons_.draw(ui_, slot.icon, ix2 + (slot.width - iconSize) / 2, iy + 2, iconSize);
      }
      ix2 += slot.width + 6;
    }
    if (icons_.has("prefs")) {
      ui_.flush();
      icons_.draw(ui_, "prefs", W - 112 + (104 - 17) / 2, 8, 17);
    }
  }
}

// ---- abas Cena/Scripts + Play (faixa entre a topbar e o viewport) ----
void EditorApp::drawTabs(int fbw, int fbh, const Theme& th) {
  if (confirmExit_ || prefsOpen_) return;
  const LayoutRects lay = layout();
  const float x = static_cast<float>(lay.leftW) + 4;
  const float y = static_cast<float>(lay.topbarH) + 3;
  const float tabW = 84, tabH = 24;

  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool clicked = down && !mouseWasDown_;

  const char* names[] = {"Cena", "Scripts"};
  for (int tab = 0; tab < 2; ++tab) {
    const float bx = x + tab * (tabW + 4);
    const bool active = activeTab_ == tab;
    ui_.rect(bx, y, tabW, tabH, active ? th.accentR : th.panelAltR, active ? th.accentG : th.panelAltG,
             active ? th.accentB : th.panelAltB);
    ui_.frame(bx, y, tabW, tabH, 1, th.lineR, th.lineG, th.lineB);
    uiText(names[tab], bx + (tabW - font_.textWidth(names[tab], 12)) / 2, y + 6, 12,
           active ? th.accentTextR : th.inkR, active ? th.accentTextG : th.inkG, active ? th.accentTextB : th.inkB);
    if (clicked && buttonHit(mx, my, bx, y, tabW, tabH)) {
      activeTab_ = tab;
      scriptPanelSel_ = -1;
    }
  }

  // Play (executa a simulação no viewport)
  {
    const float bx = x + 2 * (tabW + 4) + 8;
    const float bw = 62;
    ui_.rect(bx, y, bw, tabH, playTestOpen_ ? th.accentR : th.selR, playTestOpen_ ? th.accentG : th.selG,
             playTestOpen_ ? th.accentB : th.selB);
    ui_.frame(bx, y, bw, tabH, 1, th.lineR, th.lineG, th.lineB);
    const char* label = playTestOpen_ ? "parar" : "play";
    uiText(label, bx + (bw - font_.textWidth(label, 12)) / 2, y + 6, 12, 1.f, 1.f, 1.f);
    if (clicked && buttonHit(mx, my, bx, y, bw, tabH)) {
      if (playTestOpen_) {
        endTestPlay();
      } else {
        testPlayActive_ = false;
        testPaused_ = false;
        luaConsole_.clear();
        addHistory("teste iniciado");
        playTestOpen_ = true;
      }
    }
  }

  // Indicador de estado da simulação
  if (playTestOpen_) {
    const float sx = x + 2 * (tabW + 4) + 78;
    const char* state = testPaused_ ? "pausado" : "executando";
    uiText(std::string("● ") + state, sx, y + 6, 11, testPaused_ ? 0.85f : 0.3f, testPaused_ ? 0.7f : 0.8f, 0.25f);
  }
}

// ---- rail vertical de ferramentas (esquerda) ----
void EditorApp::drawToolRail(int fbw, int fbh, const Theme& th) {
  if (confirmExit_ || prefsOpen_) return;
  const LayoutRects lay = layout();
  const float x = 4, y = static_cast<float>(lay.topbarH) + 6;
  const float s = 34, gap = 4;
  const float panelH = 6 * s + 5 * gap + 8;

  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool clicked = down && !mouseWasDown_;

  const char* names[] = {"Add", "Select", "Move", "Scale", "Paint", "Erase"};
  const int toolOrder[6] = {1, 0, 2, 3, 4, 5};
  const char* iconNames[] = {"select", "add", "move", "scale", "paint", "erase"};

  // ===== 1ª passada: fundo do painel + fundos dos botões + cliques =====
  // A coluna inteira (do topo até a status bar) é a barra lateral.
  const float columnH = static_cast<float>(fbh - lay.topbarH - UI_STATUS_H) - 6.f;
  ui_.rect(x, y, s + 8, columnH, th.panelR, th.panelG, th.panelB);
  ui_.frame(x, y, s + 8, columnH, 1, th.lineR, th.lineG, th.lineB);
  // Divisória entre grupos de ferramentas (criação x transformação x pintura).
  for (int after : {1, 3}) {
    const float dy = y + 4 + after * (s + gap) - gap / 2.f;
    ui_.rect(x + 8, dy, s - 8, 1, th.lineR, th.lineG, th.lineB);
  }
  (void)panelH;

  float ty = y + 4;
  for (int entry = 0; entry < 6; ++entry) {
    const int tool = toolOrder[entry];
    const bool active = tool_ == tool;
    const float bx = x + 4;
    ui_.rect(bx, ty, s, s, active ? th.accentR : th.panelAltR, active ? th.accentG : th.panelAltG,
             active ? th.accentB : th.panelAltB);
    ui_.frame(bx, ty, s, s, 1, th.lineR, th.lineG, th.lineB);
    if (clicked && mx >= bx && mx <= bx + s && my >= ty && my <= ty + s) {
      tool_ = tool;
      setStatus(std::string("Ferramenta: ") + names[tool]);
    }
    ty += s + gap;
  }

  // Flush dos fundos — AQUI o batch é desenhado, então os ícones ficam por cima.
  ui_.flush();

  // ===== 2ª passada: fallbacks vetoriais primeiro (batch), flush, PNGs =====
  ty = y + 4;
  for (int entry = 0; entry < 6; ++entry) {
    const int tool = toolOrder[entry];
    const float bx = x + 4;
    // Pula os que têm PNG, MAS sempre avança ty (o `continue` antigo quebrava
    // o alinhamento e desenhava paint/erase sobre os primeiros botões).
    if (!(icons_.ready() && icons_.has(iconNames[tool]))) {
      switch (tool) {
        case 1: iconSelect(ui_, bx + 4, ty + 4, s - 8, th); break;
        case 0: ui_.rect(bx + s * 0.3f, ty + s * 0.3f, s * 0.4f, s * 0.4f, th.inkR, th.inkG, th.inkB); break;
        case 2: iconMove(ui_, bx + 4, ty + 4, s - 8, th); break;
        case 3: iconScale(ui_, bx + 4, ty + 4, s - 8, th); break;
        case 4: uiText("P", bx + s * 0.32f, ty + s * 0.24f, 15, th.inkR, th.inkG, th.inkB); break;
        case 5: uiText("X", bx + s * 0.32f, ty + s * 0.24f, 15, th.inkR, th.inkG, th.inkB); break;
        default: break;
      }
    }
    ty += s + gap;
  }
  ui_.flush();  // desenha todos os fallbacks de uma vez

  // ===== 3ª passada: PNGs (cada um com seu shader) ===== 
  if (icons_.ready()) {
    ty = y + 4;
    for (int entry = 0; entry < 6; ++entry) {
      const int tool = toolOrder[entry];
      const float bx = x + 4;
      if (icons_.has(iconNames[tool])) {
        ui_.flush();
        icons_.draw(ui_, iconNames[tool], bx + 5, ty + 5, s - 10);
      }
      ty += s + gap;
    }
  }
}

// ---- hierarquia (árvore de objetos) — painel direito superior ----
void EditorApp::drawHierarchy(int fbw, int fbh, const Theme& th) {
  if (confirmExit_ || prefsOpen_) return;
  const LayoutRects lay = layout();
  const float x = static_cast<float>(fbw - lay.rightW + 2);
  const float w = static_cast<float>(lay.rightW - 4);
  const float h = static_cast<float>(lay.viewportH()) * 0.55f;
  const float y = static_cast<float>(lay.topbarH) + 2;

  ui_.rect(x, y, w, h, th.panelR, th.panelG, th.panelB);
  ui_.frame(x, y, w, h, 1, th.lineR, th.lineG, th.lineB);
  uiText("Hierarquia", x + 10, y + 8, 13, th.inkR, th.inkG, th.inkB);

  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool clicked = down && !mouseWasDown_;

  // ===== inserção de primitivas (linha de botões) =====
  // Botões de primitiva: grade 2x2 que caiba na largura do painel.
  const char* primNames[] = {"+ caixa", "+ esfera", "+ cilindro", "+ luz"};
  const float btnW = (w - 24) / 2.f;
  for (int index = 0; index < 4; ++index) {
    const float px = x + 8 + (index % 2) * (btnW + 8);
    const float py = y + 26 + (index / 2) * 22.f;
    ui_.rect(px, py, btnW, 19, th.panelAltR, th.panelAltG, th.panelAltB);
    ui_.frame(px, py, btnW, 19, 1, th.lineR, th.lineG, th.lineB);
    uiText(primNames[index], px + 8, py + 5, 10, th.inkR, th.inkG, th.inkB);
    if (clicked && buttonHit(mx, my, px, py, btnW, 19)) {
      if (index == 3) addLight(); else addObjectPrimitive(index);
    }
  }

  // ===== busca =====
  const bool searchActive = !hierarchyFilter_.empty();
  ui_.rect(x + 8, y + 74, w - 16, 20, th.panelAltR, th.panelAltG, th.panelAltB);
  ui_.frame(x + 8, y + 74, w - 16, 20, 1, th.lineR, th.lineG, th.lineB);
  uiText(searchActive ? ("busca: " + hierarchyFilter_) : "buscar objetos...",
         x + 14, y + 78, 10, searchActive ? th.inkR : th.inkSoftR, searchActive ? th.inkG : th.inkSoftG, searchActive ? th.inkB : th.inkSoftB);
  if (clicked && buttonHit(mx, my, x + 8, y + 74, w - 16, 20)) {
    FILE* pipe = popen("zenity --entry --title='Buscar objetos' --text='Nome/grupo:' 2>/dev/null | tr -d '\n'", "r");
    char buffer[64] = {};
    if (pipe && fgets(buffer, sizeof(buffer), pipe)) hierarchyFilter_ = buffer;
    else hierarchyFilter_.clear();
    if (pipe) pclose(pipe);
  }

  // ===== lista =====
  float ly = y + 100;
  const int maxShow = static_cast<int>((h - 112) / 20.f);
  int shown = 0;
  if (blockHidden_.size() != world_.blocks.size()) blockHidden_.resize(world_.blocks.size(), false);
  if (blockLocked_.size() != world_.blocks.size()) blockLocked_.resize(world_.blocks.size(), false);
  if (blockGroups_.size() != world_.blocks.size()) blockGroups_.resize(world_.blocks.size());

  for (size_t index = 0; index < world_.blocks.size() && shown < maxShow; ++index) {
    const Block& block = world_.blocks[index];
    const std::string group = blockGroups_[index].empty() ? "raiz" : blockGroups_[index];
    // filtro de busca por grupo ou índice
    if (searchActive) {
      const std::string needle = hierarchyFilter_;
      char indexText[16];
      std::snprintf(indexText, sizeof(indexText), "part%zu", index + 1);
      if (group.find(needle) == std::string::npos && needle != indexText) continue;
    }
    const bool selected = std::find(selection_.begin(), selection_.end(), static_cast<int>(index)) != selection_.end();
    if (selected) ui_.rect(x + 2, ly, w - 4, 18, th.selR * 0.25f, th.selG * 0.25f, th.selB * 0.25f);

    // ícone colorido
    ui_.rect(x + 10, ly + 4, 10, 10, block.color.r / 255.f, block.color.g / 255.f, block.color.b / 255.f);
    // nome (truncado para não invadir o grupo)
    char label[64];
    std::snprintf(label, sizeof(label), "Part%zu", index + 1);
    const std::string name = label;
    std::string shownName = name;
    const float nameMax = w - 118;
    while (font_.textWidth(shownName, 11) > nameMax && shownName.size() > 4) shownName.pop_back();
    uiText(shownName, x + 28, ly + 4, 11, blockHidden_[index] ? th.inkSoftR : (selected ? th.inkR : th.inkSoftR),
           blockHidden_[index] ? th.inkSoftG : (selected ? th.inkG : th.inkSoftG),
           blockHidden_[index] ? th.inkSoftB : (selected ? th.inkB : th.inkSoftB));

    // grupo (truncado)
    std::string shownGroup = group;
    while (font_.textWidth(shownGroup, 9) > 56 && shownGroup.size() > 3) shownGroup.pop_back();
    uiText(shownGroup, x + w - 102, ly + 4, 9, th.inkSoftR, th.inkSoftG, th.inkSoftB);

    // clique no nome: seleciona (se não travado)
    if (clicked && buttonHit(mx, my, x, ly, w - 60, 18)) {
      if (blockLocked_[index]) {
        setStatus("Objeto " + std::to_string(index) + " travado — destrave para editar.");
      } else {
        selected_ = static_cast<int>(index);
        selection_.clear();
        selection_.push_back(selected_);
      }
    }

    // pastilhas de estado H/L: logo após o grupo, com cor viva quando ativas
    {
      const float bx = x + w - 50;
      // H — oculto
      ui_.rect(bx, ly + 3, 18, 13, blockHidden_[index] ? 0.78f : th.panelAltR,
               blockHidden_[index] ? 0.52f : th.panelAltG, blockHidden_[index] ? 0.15f : th.panelAltB);
      ui_.frame(bx, ly + 3, 18, 13, 1, blockHidden_[index] ? 0.78f : th.lineR,
                blockHidden_[index] ? 0.52f : th.lineG, blockHidden_[index] ? 0.15f : th.lineB);
      uiText("H", bx + 6, ly + 5, 9, blockHidden_[index] ? 1.f : th.inkSoftR,
             blockHidden_[index] ? 1.f : th.inkSoftG, blockHidden_[index] ? 1.f : th.inkSoftB);
      if (clicked && buttonHit(mx, my, bx, ly + 2, 20, 14)) toggleHidden(static_cast<int>(index));

      // L — travado
      const float bx2 = bx + 21;
      ui_.rect(bx2, ly + 3, 18, 13, blockLocked_[index] ? 0.72f : th.panelAltR,
               blockLocked_[index] ? 0.22f : th.panelAltG, blockLocked_[index] ? 0.18f : th.panelAltB);
      ui_.frame(bx2, ly + 3, 18, 13, 1, blockLocked_[index] ? 0.72f : th.lineR,
                blockLocked_[index] ? 0.22f : th.lineG, blockLocked_[index] ? 0.18f : th.lineB);
      uiText("L", bx2 + 6, ly + 5, 9, blockLocked_[index] ? 1.f : th.inkSoftR,
             blockLocked_[index] ? 1.f : th.inkSoftG, blockLocked_[index] ? 1.f : th.inkSoftB);
      if (clicked && buttonHit(mx, my, bx2, ly + 2, 20, 14)) toggleLocked(static_cast<int>(index));
    }
    ly += 20;
    ++shown;
  }
  if (shown == 0) uiText("(nenhum objeto)", x + 12, ly + 4, 11, th.inkSoftR, th.inkSoftG, th.inkSoftB);
}

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
