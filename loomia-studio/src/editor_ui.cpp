// Toda a UI 2D: welcome screen, toolbar, menus (Arquivo/Cena/Mais cores),
// linha de status, retângulo de seleção, modal de alterações não salvas e as
// prévias 3D dos templates.
#include "editor.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cmath>

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace loomia {
void EditorApp::uiText(const std::string& str, float x, float y, float px, float r, float g, float b, float a) {
  textQueue_.push_back({str, x, y, px, r, g, b, a});
}

void EditorApp::flushText() {
  if (textQueue_.empty()) return;
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  for (const TextDraw& item : textQueue_) {
    if (font_.ready()) {
      font_.draw(item.str, item.x, item.y, item.size, item.r, item.g, item.b, item.a);
    } else {
      ui_.text(item.str, item.x, item.y, item.size / 5.f, item.r, item.g, item.b, item.a);
    }
  }
  textQueue_.clear();
  ui_.flush();  // fallback bitmap acumula no overlay
}

int EditorApp::buttonHit(double mx, double my, float x, float y, float w, float h) const {
  return (mx >= x && mx <= x + w && my >= y && my <= y + h) ? 1 : 0;
}

// ------------------------------------------------------------------ loop

void EditorApp::drawUi(int fbw, int fbh) {
  const float W = static_cast<float>(fbw), H = static_cast<float>(fbh);
  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool clicked = down && !mouseWasDown_;

  // ---------- abas Cena/Scripts (abaixo da toolbar) ----------
  {
    const float tabY = static_cast<float>(UI_BAR_H) + 6.f;
    const float tabW = 96.f, tabH = 34.f;
    float tabX = 12.f;
    const char* tabNames[] = {"Cena", "Scripts"};
    for (int tab = 0; tab < 2; ++tab) {
      const bool active = activeTab_ == tab;
      ui_.rect(tabX, tabY, tabW, tabH, active ? 0.91f : 0.99f, active ? 0.71f : 0.99f, active ? 0.17f : 1.f);
      ui_.frame(tabX, tabY, tabW, tabH, 2, 0.08f, 0.12f, 0.17f);
      uiText(tabNames[tab], tabX + (tabW - font_.textWidth(tabNames[tab], 14)) / 2, tabY + 10, 14,
             active ? 0.15f : 0.35f, active ? 0.12f : 0.4f, 0.02f);
      if (clicked && buttonHit(mx, my, tabX, tabY, tabW, tabH)) {
        activeTab_ = tab;
        scriptPanelSel_ = -1;
      }
      tabX += tabW + 6.f;
    }

    // Botão Play (janela separada, mesma engine; Ctrl+P também dispara)
    {
      const float px = tabX + 14.f;
      ui_.rect(px, tabY, 92.f, tabH, 0.91f, 0.71f, 0.17f);
      ui_.frame(px, tabY, 92.f, tabH, 2, 0.08f, 0.12f, 0.17f);
      uiText("▶ Play", px + (92.f - font_.textWidth("▶ Play", 14)) / 2, tabY + 10, 14, 0.15f, 0.12f, 0.f);
      if (clicked && buttonHit(mx, my, px, tabY, 92.f, tabH)) runPlayTest();
    }

    // Sidebar adaptável (abaixo das abas, ao lado da área de trabalho)
    if (activeTab_ == 0) drawScenePanel(fbw, fbh);
    else drawScriptsPanel(fbw, fbh);
  }

  // ---------- preferências (⚙, canto direito abaixo da toolbar) ----------
  {
    const float px = static_cast<float>(fbw) - 136.f, py = static_cast<float>(UI_BAR_H) + 6.f;
    ui_.rect(px, py, 124.f, 34.f, 0.97f, 0.97f, 1.f);
    ui_.frame(px, py, 124.f, 34.f, 2, 0.08f, 0.12f, 0.17f);
    uiText("⚙ Opções", px + 26, py + 10, 13, 0.15f, 0.2f, 0.3f);
    if (clicked && buttonHit(mx, my, px, py, 124.f, 34.f)) prefsOpen_ = !prefsOpen_;
    if (prefsOpen_) drawPrefs(px, py + 40);
  }

  // ---------- barra de ferramentas superior
  const float barY = 0;
  ui_.rect(0, barY, W, static_cast<float>(UI_BAR_H), 1.f, 0.99f, 0.96f);
  ui_.frame(0, barY, W, static_cast<float>(UI_BAR_H), 2, 0.08f, 0.12f, 0.17f);

  // ---------- menu Arquivo (dropdown)
  {
    const float fx = 12.f, fy = barY + 12.f, fw = 92.f, fh = 40.f;
    ui_.rect(fx, fy, fw, fh, fileMenu_ ? 0.91f : 0.96f, fileMenu_ ? 0.71f : 0.96f, fileMenu_ ? 0.17f : 0.96f);
    ui_.frame(fx, fy, fw, fh, 2, 0.08f, 0.12f, 0.17f);
    uiText("Arquivo", fx + 16, fy + 12, 14, fileMenu_ ? 0.15f : 0.3f, fileMenu_ ? 0.12f : 0.35f, 0.02f);
    if (clicked && buttonHit(mx, my, fx, fy, fw, fh)) fileMenu_ = !fileMenu_;

    if (fileMenu_) {
      const float mw = 220;
      float my2 = fy + fh + 4;
      struct Item { const char* label; int action; };
      const Item items[] = {
          {"Novo (Ctrl+N)", 0},
          {"Abrir .loom", 1},
          {"Salvar (Ctrl+S)", 2},
          {"Salvar como...", 3},
          {"Criar cópia...", 4},
          {"Renomear mapa", 5},
          {"Voltar ao início", 6},
      };
      for (const Item& item : items) {
        ui_.rect(fx, my2, mw, 30, 1.f, 1.f, 1.f);
        ui_.frame(fx, my2, mw, 30, 1, 0.5f, 0.55f, 0.6f);
        uiText(item.label, fx + 12, my2 + 7, 13, 0.15f, 0.2f, 0.28f);
        if (clicked && buttonHit(mx, my, fx, my2, mw, 30)) {
          fileMenu_ = false;
          switch (item.action) {
            case 0: newFromTemplate(0); break;
            case 1: {
              FILE* pipe = popen("zenity --file-selection --title='Abrir mapa .loom' 2>/dev/null | tr -d '\n'", "r");
              char buffer[1024] = {};
              bool opened = false;
              if (pipe && fgets(buffer, sizeof(buffer), pipe)) {
                std::string chosen = buffer;
                if (!chosen.empty()) {
                  std::string err;
                  if (openLoom(chosen, err)) { addRecent(chosen); opened = true; }
                  else setStatus("Erro: " + err);
                }
              } else setStatus("Nenhum arquivo escolhido.");
              if (pipe) pclose(pipe);
              break;
            }
            case 2: {
              if (!filePath_.empty()) {
                std::string err;
                if (saveLoom(filePath_, err)) addRecent(filePath_);
                else setStatus("Erro ao salvar: " + err);
              } else saveWithDialog();
              break;
            }
            case 3: saveWithDialog(); break;
            case 4: saveCopy(); break;
            case 5: renameMap(); break;
            case 6: backToWelcome(); break;
          }
        }
        my2 += 34;
      }

      // Recentes
      uiText("Recentes:", fx + 12, my2 + 6, 12, 0.5f, 0.55f, 0.6f);
      my2 += 28;
      if (recentList_.empty()) uiText("(vazio)", fx + 12, my2, 12, 0.6f, 0.65f, 0.7f);
      for (const std::string& path : recentList_) {
        // Mostra só o nome do arquivo.
        const std::string name = path.substr(path.find_last_of('/') + 1);
        ui_.rect(fx, my2, mw, 28, 0.97f, 0.97f, 1.f);
        ui_.frame(fx, my2, mw, 28, 1, 0.6f, 0.65f, 0.7f);
        uiText(name, fx + 12, my2 + 7, 12, 0.2f, 0.3f, 0.5f);
        if (clicked && buttonHit(mx, my, fx, my2, mw, 28)) {
          fileMenu_ = false;
          std::string err;
          if (openLoom(path, err)) setStatus("Mapa carregado: " + path);
          else setStatus("Erro: " + err);
        }
        my2 += 32;
      }
    }
  }

  // ferramentas começam depois do menu Arquivo
  const char* toolNames[] = {"1 Add", "2 Select", "3 Move", "4 Scale", "5 Paint", "6 Erase"};
  for (int index = 0; index < 6; ++index) {
    const float x = 118.f + index * 86.f;
    const float y = barY + 12.f;
    const bool active = tool_ == index;
    ui_.rect(x, y, 80.f, 40.f, active ? 0.91f : 0.96f, active ? 0.71f : 0.96f, active ? 0.17f : 0.96f);
    ui_.frame(x, y, 80.f, 40.f, 2, 0.08f, 0.12f, 0.17f);
    uiText(toolNames[index], x + 10, y + 14, 13, active ? 0.15f : 0.3f, active ? 0.12f : 0.35f, 0.02f);
    if (clicked && buttonHit(mx, my, x, y, 80.f, 40.f)) { tool_ = index; setStatus(std::string("Ferramenta: ") + toolNames[index]); }
  }

  // ---------- paleta de cores (10 básicas) — logo após as ferramentas
  const unsigned char palette[][3] = {
      {230, 120, 60}, {230, 190, 40}, {100, 160, 60}, {70, 150, 200}, {110, 80, 190},
      {230, 230, 235}, {70, 70, 80}, {190, 60, 60},  {240, 160, 200}, {60, 200, 170}};
  for (int index = 0; index < 10; ++index) {
    const float x = 640.f + index * 30.f;
    const float y = barY + 12.f;
    const bool match = currentColor_[0] == palette[index][0] && currentColor_[1] == palette[index][1] && currentColor_[2] == palette[index][2];
    ui_.rect(x, y, 24.f, 24.f, palette[index][0] / 255.f, palette[index][1] / 255.f, palette[index][2] / 255.f, 1.f);
    ui_.frame(x, y, 24.f, 24.f, match ? 3.f : 1.f, 0.08f, 0.12f, 0.17f);
    if (clicked && buttonHit(mx, my, x, y, 24.f, 24.f)) {
      currentColor_[0] = palette[index][0]; currentColor_[1] = palette[index][1]; currentColor_[2] = palette[index][2];
    }
  }

  // ---------- botão Cena (cor do céu) — após a paleta
  {
    const float x = 950.f, y = barY + 12.f;
    ui_.rect(x, y, 76.f, 40.f, 0.96f, 0.96f, 0.96f);
    ui_.frame(x, y, 76.f, 40.f, 2, 0.08f, 0.12f, 0.17f);
    uiText("Cena (C)", x + 12, y + 14, 13, 0.3f, 0.35f, 0.42f);
    if (clicked && buttonHit(mx, my, x, y, 76.f, 40.f)) sceneMenu_ = !sceneMenu_;
  }

  // ---------- botão "Mais cores" (dropdown 256 cores HSL) — após o Cena
  {
    const float x = 1036.f, y = barY + 12.f;
    ui_.rect(x, y, 96.f, 40.f, colorPicker_ ? 0.91f : 0.96f, colorPicker_ ? 0.71f : 0.96f, colorPicker_ ? 0.17f : 0.96f);
    ui_.frame(x, y, 96.f, 40.f, 2, 0.08f, 0.12f, 0.17f);
    uiText("Mais cores", x + 12, y + 14, 13, colorPicker_ ? 0.15f : 0.3f, colorPicker_ ? 0.12f : 0.35f, 0.02f);
    if (clicked && buttonHit(mx, my, x, y, 96.f, 40.f)) colorPicker_ = !colorPicker_;

    if (colorPicker_) {
      // Grid 16x16 de hue x value, saturação fixa (256 cores).
      const float gw = 256.f, cell = gw / 16.f;
      const float gx = x, gy = y + 46.f;
      ui_.rect(gx - 4, gy - 4, gw + 8, gw + 8, 1.f, 1.f, 1.f);
      ui_.frame(gx - 4, gy - 4, gw + 8, gw + 8, 2, 0.08f, 0.12f, 0.17f);
      for (int row = 0; row < 16; ++row) {
        for (int col = 0; col < 16; ++col) {
          float rgb[3];
          const float hue = col / 16.f;
          const float val = 1.f - row / 15.f;
          hslToRgb(hue, 0.8f, val, rgb);
          ui_.rect(gx + col * cell, gy + row * cell, cell, cell, rgb[0] / 255, rgb[1] / 255, rgb[2] / 255);
        }
      }
      // Clique escolhe a cor.
      if (down && mx > gx && mx < gx + gw && my > gy && my < gy + gw) {
        const int col = std::clamp(static_cast<int>((mx - gx) / cell), 0, 15);
        const int row = std::clamp(static_cast<int>((my - gy) / cell), 0, 15);
        float rgb[3];
        hslToRgb(col / 16.f, 0.8f, 1.f - row / 15.f, rgb);
        currentColor_[0] = static_cast<int>(rgb[0]);
        currentColor_[1] = static_cast<int>(rgb[1]);
        currentColor_[2] = static_cast<int>(rgb[2]);
      }
      // Fecha ao clicar fora do grid e fora do botão.
      if (clicked && !(mx > gx - 4 && mx < gx + gw + 4 && my > gy - 4 && my < gy + gw + 4) &&
          !buttonHit(mx, my, x, y, 96.f, 40.f)) {
        colorPicker_ = false;
      }
    }
  }

  // ---------- linha de status
  ui_.rect(0, H - UI_STATUS_H, W, static_cast<float>(UI_STATUS_H), 0.08f, 0.12f, 0.17f);
  char left[256];
  std::snprintf(left, sizeof(left), "%s %s  |  %zu blocos  |  %s", dirty_ ? "*" : " ", filePath_.empty() ? "(sem titulo)" : filePath_.c_str(), world_.blocks.size(), status_.c_str());
  uiText(left, 10, H - UI_STATUS_H + 5, 13, 0.95f, 0.9f, 0.8f);

  // ---------- atalhos globais (keyboard)
  const bool ctrl = keys_[341] /*GLFW_KEY_LEFT_CONTROL*/ || keys_[345];
  const bool shift = keys_[340] || keys_[344];
  if (ctrl && keys_['Z']) { undo(); keys_['Z'] = false; }
  if (ctrl && shift && keys_['Z']) { redo(); keys_['Z'] = false; }
  if (ctrl && keys_['Y']) { redo(); keys_['Y'] = false; }
  if (ctrl && keys_['D']) { duplicateSelection(); keys_['D'] = false; }
  if (keys_[256] /*DELETE*/ || keys_[261] /*BACKSPACE*/) { deleteSelection(); keys_[256] = false; keys_[261] = false; }
  if (keys_['G']) { snapSelection(); keys_['G'] = false; }
  if (keys_['F']) { focusSelection(); keys_['F'] = false; }
  if (keys_['C']) { sceneMenu_ = !sceneMenu_; keys_['C'] = false; }
  if (keys_['X']) { gizmoAxis_ = 0; keys_['X'] = false; }
  if (keys_['Y']) { gizmoAxis_ = 1; keys_['Y'] = false; }
  if (keys_['Z']) { gizmoAxis_ = 2; keys_['Z'] = false; }
  if (keys_['1']) { tool_ = 0; keys_['1'] = false; }
  if (keys_['2']) { tool_ = 1; keys_['2'] = false; }
  if (keys_['3']) { tool_ = 2; keys_['3'] = false; }
  if (keys_['4']) { tool_ = 3; keys_['4'] = false; }
  if (keys_['5']) { tool_ = 4; keys_['5'] = false; }
  if (keys_['6']) { tool_ = 5; keys_['6'] = false; }

  // Ctrl+S: salva (se tem caminho) ou abre dialog de salvar
  if (ctrl && keys_['S']) {
    keys_['S'] = false;
    std::string target = filePath_;
    if (target.empty()) {
      FILE* pipe = popen("zenity --file-selection --save --confirm-overwrite --title='Salvar mapa .loom' 2>/dev/null | tr -d '\n'", "r");
      char buffer2[1024] = {};
      if (pipe && fgets(buffer2, sizeof(buffer2), pipe)) target = buffer2;
      if (pipe) pclose(pipe);
    }
    if (!target.empty()) {
      if (target.find(".loom") == std::string::npos) target += ".loom";
      std::string err;
      if (!saveLoom(target, err)) setStatus("Erro ao salvar: " + err);
    }
  }

  // Ctrl+N: novo mapa (baseplate)
  if (ctrl && keys_['N']) { keys_['N'] = false; newFromTemplate(0); }

}

// ---------------------------------------------------------------------------
// Gizmos e menu de cena
// ---------------------------------------------------------------------------

void EditorApp::drawWelcome(int fbw, int fbh) {
  // Fundo suave
  ui_.rect(0, 0, static_cast<float>(fbw), static_cast<float>(fbh), 0.94f, 0.93f, 0.88f, 1.f);
  uiText("Loomia Studio", fbw / 2.f - font_.textWidth("Loomia Studio", 42) / 2, 100, 42, 0.08f, 0.12f, 0.17f);
  uiText("Crie e edite seus próprios mundos, depois, compartilhe com seus amigos!", fbw / 2.f - font_.textWidth("Crie e edite seus próprios mundos, depois, compartilhe com seus amigos!", 18) / 2, 160, 18, 0.3f, 0.35f, 0.42f);

  // Cartões de template
  const char* names[] = {"Baseplate", "Ilhas Flutuantes", "Obby (Parkour)", "Sala de Exposicao"};
  const char* descs[] = {"piso limpo para comecar do zero", "tres ilhas no ceu com pontes", "plataformas suspensas", "salao com todos os materiais"};
  const float cardW = 250, cardH = 180;
  const float startX = fbw / 2.f - (cardW * 4 + 40) / 2.f;
  for (int index = 0; index < 4; ++index) {
    const float x = startX + index * (cardW + 10);
    const float y = 280;
    ui_.rect(x, y, cardW, cardH, 1.f, 1.f, 1.f);
    ui_.frame(x, y, cardW, cardH, 3, 0.08f, 0.12f, 0.17f);
    // moldura da prévia (a imagem 3D é desenhada por drawTemplatePreviews)
    ui_.frame(x + 16, y + 16, cardW - 32, 66, 2, 0.4f, 0.5f, 0.6f);
    uiText(names[index], x + 16, y + 92, 16, 0.08f, 0.12f, 0.17f);
    uiText(descs[index], x + 16, y + 116, 12, 0.35f, 0.4f, 0.46f);
    uiText("[clique para comecar]", x + 16, y + 148, 12, 0.72f, 0.55f, 0.1f);
    // clique (com consumo — um widget por frame)
    double mx, my;
    glfwGetCursorPos(window_, &mx, &my);
    if (!clickConsumed_ && uiCooldown_ == 0 && buttonHit(mx, my, x, y, cardW, cardH) &&
        glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
      clickConsumed_ = true;
      newFromTemplate(index);
      return;
    }
  }

  // Carregar arquivo
  uiText("ou abra um mapa existente:", fbw / 2.f - font_.textWidth("ou abra um mapa existente:", 14) / 2, 520, 14, 0.3f, 0.35f, 0.42f);
  const float bx = fbw / 2.f - 110, by = 550, bw = 220, bh = 40;
  ui_.rect(bx, by, bw, bh, 0.91f, 0.71f, 0.17f);
  ui_.frame(bx, by, bw, bh, 3, 0.08f, 0.12f, 0.17f);
  uiText("ABRIR .LOOM", bx + (bw - font_.textWidth("ABRIR .LOOM", 16)) / 2, by + 14, 16, 0.15f, 0.12f, 0.0f);
  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  if (down && !mouseWasDown_ && !clickConsumed_ && buttonHit(mx, my, bx, by, bw, bh)) {
    clickConsumed_ = true;
    // File dialog nativo via zenity/kdialog (padrão em distros Linux)
    FILE* pipe = popen("zenity --file-selection --title='Abrir mapa .loom' 2>/dev/null | tr -d '\\n'", "r");
    char buffer[1024] = {};
    if (pipe && fgets(buffer, sizeof(buffer), pipe)) {
      std::string chosen = buffer;
      if (!chosen.empty()) {
        std::string err;
        if (!openLoom(chosen, err)) setStatus("Erro: " + err);
      }
    } else {
      setStatus("Nenhum arquivo escolhido (ou zenity nao instalado).");
    }
    if (pipe) pclose(pipe);
  }
}

// ------------------------------------------------------------------ UI do editor

void EditorApp::drawSceneMenu(float fbw, float fbh) {
  (void)fbh;
  if (!sceneMenu_) return;
  const float W = 260, H = 130;
  const float x = static_cast<float>(fbw) - W - 16, y = 60;
  ui_.rect(x, y, W, H, 1.f, 1.f, 1.f);
  ui_.frame(x, y, W, H, 3, 0.08f, 0.12f, 0.17f);
  uiText("Cena", x + 14, y + 12, 16, 0.08f, 0.12f, 0.17f);
  uiText("Cor do ceu (hue)", x + 14, y + 40, 12, 0.3f, 0.35f, 0.42f);

  // Slider de hue: arraste para mudar a cor do céu.
  const float sx = x + 14, sy = y + 60, sw = W - 28;
  ui_.rect(sx, sy, sw, 10, 0.85f, 0.9f, 0.95f);
  for (int step = 0; step < 12; ++step) {
    float rgb[3];
    hslToRgb(step / 12.f, skySat_, skyVal_, rgb);
    ui_.rect(sx + step * sw / 12.f, sy, sw / 12.f, 10, rgb[0] / 255, rgb[1] / 255, rgb[2] / 255);
  }
  const float handleX = sx + skyHue_ * sw - 4;
  ui_.rect(handleX, sy - 3, 8, 16, 0.1f, 0.15f, 0.2f);

  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS &&
      mx > sx && mx < sx + sw && my > sy - 6 && my < sy + 16) {
    const float newHue = std::clamp(static_cast<float>((mx - sx) / sw), 0.f, 0.999f);
    if (std::abs(newHue - skyHue_) > 0.002f) {
      skyHue_ = newHue;
      float rgb[3];
      hslToRgb(skyHue_, skySat_, skyVal_, rgb);
      world_.sky = {rgb[0], rgb[1], rgb[2]};
      markDirty();
    }
  }

  uiText("Clique fora para fechar", x + 14, y + 92, 11, 0.5f, 0.55f, 0.6f);
  // Fecha ao clicar fora do menu.
  if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS &&
      !(mx > x && mx < x + W && my > y && my < y + H)) {
    sceneMenu_ = false;
  }
}

// Gizmo dos minicubos (6 faces do bloco âncora).
//
// Dividido em duas fases:
//   • updateGizmoTryPick — no CLIQUE: testa as hitboxes dos 6 minicubos e
//     inicia o arrasto (devolve true se pegou, consumindo o clique);
//   • updateGizmoDrag — nos frames seguintes: aplica o delta do mouse no eixo
//     escolhido, usando o ponto do raio a uma distância fixa (a do clique),
//     o que mantém o arrasto estável em qualquer ângulo de câmera.
void EditorApp::drawConfirmExit(int fbw, int fbh) {
  if (!confirmExit_) return;
  // Fundo escurecido.
  ui_.rect(0, 0, static_cast<float>(fbw), static_cast<float>(fbh), 0.05f, 0.08f, 0.12f, 0.55f);
  const float W = 460, H = 190;
  const float x = (fbw - W) / 2.f, y = (fbh - H) / 2.f;
  ui_.rect(x, y, W, H, 1.f, 1.f, 1.f);
  ui_.frame(x, y, W, H, 3, 0.08f, 0.12f, 0.17f);
  uiText("Alterações não salvas", x + 24, y + 20, 20, 0.75f, 0.3f, 0.1f);
  uiText("O mapa tem alterações que ainda não foram salvas.", x + 24, y + 56, 14, 0.3f, 0.35f, 0.42f);

  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool clicked = down && !mouseWasDown_;

  struct Option { const char* label; float x; };
  const float bw = 130;
  const Option options[] = {
      {"Salvar", x + 24},
      {"Não salvar", x + 24 + bw + 10},
      {"Cancelar", x + 24 + (bw + 10) * 2},
  };
  for (const Option& option : options) {
    ui_.rect(option.x, y + 120, bw, 42, 0.96f, 0.96f, 0.96f);
    ui_.frame(option.x, y + 120, bw, 42, 2, 0.08f, 0.12f, 0.17f);
    uiText(option.label, option.x + (bw - font_.textWidth(option.label, 14)) / 2, y + 133, 14, 0.15f, 0.2f, 0.28f);
    if (clicked && !clickConsumed_ && buttonHit(mx, my, option.x, y + 120, bw, 42)) {
      clickConsumed_ = true;
      confirmExit_ = false;
      uiCooldown_ = 12;  // ignora cliques por ~0.2s: nada vaza para a welcome
      if (std::strcmp(option.label, "Salvar") == 0) {
        if (!filePath_.empty()) {
          std::string err;
          if (saveLoom(filePath_, err)) { addRecent(filePath_); inEditor_ = false; if (glfwWindowShouldClose(window_)) exitRequested_ = true; }
          else setStatus("Erro ao salvar: " + err);
        } else if (saveWithDialog()) {
          inEditor_ = false;
          if (glfwWindowShouldClose(window_)) exitRequested_ = true;
        }
      } else if (std::strcmp(option.label, "Não salvar") == 0) {
        dirty_ = false;
        // Se o pedido veio do X da janela, fecha de verdade.
        if (glfwWindowShouldClose(window_)) exitRequested_ = true;
        inEditor_ = false;
      }
      // "Cancelar" só fecha o modal.
    }
  }
}

void EditorApp::drawTemplatePreviews(int fbw, int fbh) {
  if (!previewReady_) return;
  const float cardW = 250, cardH = 180;
  const float startX = static_cast<float>(fbw) / 2.f - (cardW * 4 + 40) / 2.f;
  const float cardY = 280;
  const float pvW = cardW - 32, pvH = 66;
  const float pvY = cardY + 16;

  const TemplateId ids[] = {TemplateId::Baseplate, TemplateId::SkyIslands, TemplateId::Obby, TemplateId::Showcase};
  for (int index = 0; index < 4; ++index) {
    World preview = makeTemplate(ids[index]);
    previewRenderer_.upload(preview);

    const float pvX = startX + static_cast<float>(index) * (cardW + 10) + 16;
    const int vx = static_cast<int>(pvX);
    const int vy = static_cast<int>(fbh - pvY - pvH);
    glViewport(vx, vy, static_cast<int>(pvW), static_cast<int>(pvH));
    glScissor(vx, vy, static_cast<int>(pvW), static_cast<int>(pvH));
    glEnable(GL_SCISSOR_TEST);

    const float aspect = pvW / pvH;
    const float yaw = 0.9f + index * 0.35f;
    const Vec3 eye{26.f * std::sin(yaw), 16.f, 26.f * std::cos(yaw)};
    Camera camera;
    camera.position = eye;
    camera.fovDegrees = 55.f;
    previewRenderer_.draw(camera, aspect);

    glDisable(GL_SCISSOR_TEST);
  }
  glViewport(0, 0, fbw, fbh);
}

// ---------------------------------------------------------------------------
// Arquivo: recentes, salvar como, cópia, renomear, modal de saída
// ---------------------------------------------------------------------------

std::string recentsPath() {
  const char* home = std::getenv("HOME");
  return (home ? std::string(home) : std::string("/tmp")) + "/.loomia-studio-recentes.txt";
}

void EditorApp::hslToRgb(float h, float s, float v, float out[3]) const {
  h = h - std::floor(h);
  float r = v, g = v, b = v;
  if (s > 0) {
    const float chroma = (1 - std::abs(2 * v - 1)) * s;
    const float hue = h * 6;
    const float x = chroma * (1 - std::abs(std::fmod(hue, 2.f) - 1));
    if (hue < 1)      { r = chroma; g = x; }
    else if (hue < 2) { r = x; g = chroma; }
    else if (hue < 3) { g = chroma; b = x; }
    else if (hue < 4) { g = x; b = chroma; }
    else if (hue < 5) { r = x; b = chroma; }
    else              { r = chroma; b = x; }
    const float m = v - chroma / 2;
    r += m; g += m; b += m;
  }
  out[0] = r * 255; out[1] = g * 255; out[2] = b * 255;
}

// Preferências do editor — popup expandido: aparência, câmera, gizmos,
// comportamento da interface. Nunca altera o mapa nem o jogo.
void EditorApp::drawPrefs(float x, float y) {
  const float w = 320.f, h = 320.f;
  ui_.rect(x, y, w, h, th_.panelR, th_.panelG, th_.panelB);
  ui_.frame(x, y, w, h, 2, th_.lineR, th_.lineG, th_.lineB);
  uiText("Preferências do Editor", x + 14, y + 12, 15, th_.inkR, th_.inkG, th_.inkB);
  ui_.rect(x, y + 32, w, 1, th_.lineR, th_.lineG, th_.lineB);

  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool clicked = down && !mouseWasDown_;

  float ly = y + 44;
  auto toggle = [&](const char* label, bool& value) {
    uiText(value ? std::string("[x] ") + label : std::string("[ ] ") + label, x + 14, ly, 12, th_.inkR, th_.inkG, th_.inkB);
    if (clicked && buttonHit(mx, my, x + 10, ly - 3, w - 20, 18)) value = !value;
    ly += 22;
  };

  uiText("Aparência", x + 14, ly, 11, th_.inkSoftR, th_.inkSoftG, th_.inkSoftB);
  ly += 18;
  toggle("tema escuro da interface", prefsDark_);
  toggle("grade visível no viewport", prefsShowGrid_);

  uiText("Edição", x + 14, ly, 11, th_.inkSoftR, th_.inkSoftG, th_.inkSoftB);
  ly += 18;
  toggle("snap ao criar blocos", prefsSnapAlways_);
  toggle("mostrar gizmos de face", prefsShowGizmos_);

  uiText("Câmera", x + 14, ly, 11, th_.inkSoftR, th_.inkSoftG, th_.inkSoftB);
  ly += 18;
  toggle("inverter eixo da órbita", prefsInvertOrbit_);

  char label[64];
  std::snprintf(label, sizeof(label), "largura do painel direito: %d px", sidebarWidth_);
  uiText(label, x + 14, ly, 12, th_.inkR, th_.inkG, th_.inkB);
  ly += 16;
  ui_.rect(x + 14, ly, w - 28, 10, th_.panelAltR, th_.panelAltG, th_.panelAltB);
  ui_.rect(x + 14 + (sidebarWidth_ - 180) * (w - 28) / 420.f, ly - 2, 12, 14, th_.selR, th_.selG, th_.selB);
  if (down && my > ly - 4 && my < ly + 18 && mx > x + 14 && mx < x + w - 14) {
    const float t = std::clamp(static_cast<float>((mx - x - 14) / (w - 28)), 0.f, 1.f);
    sidebarWidth_ = 180 + static_cast<int>(t * 420);
  }
  ly += 26;

  std::snprintf(label, sizeof(label), "sensibilidade da órbita: %.2f", prefsOrbitSpeed_);
  uiText(label, x + 14, ly, 12, th_.inkR, th_.inkG, th_.inkB);
  ly += 16;
  ui_.rect(x + 14, ly, w - 28, 10, th_.panelAltR, th_.panelAltG, th_.panelAltB);
  const float handle = std::clamp((prefsOrbitSpeed_ - 0.2f) / 1.8f, 0.f, 1.f);
  ui_.rect(x + 14 + handle * (w - 40), ly - 2, 12, 14, th_.selR, th_.selG, th_.selB);
  if (down && my > ly - 4 && my < ly + 18 && mx > x + 14 && mx < x + w - 14) {
    const float t = std::clamp(static_cast<float>((mx - x - 14) / (w - 28)), 0.f, 1.f);
    prefsOrbitSpeed_ = 0.2f + t * 1.8f;
  }
  ly += 26;

  uiText("Essas opções não afetam o mapa nem o jogo.", x + 14, ly, 11, th_.inkSoftR, th_.inkSoftG, th_.inkSoftB);

  if (clicked && !(mx > x && mx < x + w && my > y && my < y + h)) prefsOpen_ = false;
}

}  // namespace loomia
