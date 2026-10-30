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

void EditorApp::drawWelcome(int fbw, int fbh) {
  // Fundo suave
  ui_.rect(0, 0, static_cast<float>(fbw), static_cast<float>(fbh), 0.94f, 0.93f, 0.88f, 1.f);
  uiText("Loomia Studio", fbw / 2.f - font_.textWidth("Loomia Studio", 42) / 2, 100, 42, 0.08f, 0.12f, 0.17f);
  uiText("Crie e edite seus próprios mundos, depois, compartilhe com seus amigos!", fbw / 2.f - font_.textWidth("Crie e edite seus próprios mundos, depois, compartilhe com seus amigos!", 18) / 2, 160, 18, 0.3f, 0.35f, 0.42f);

  // Cartões de template (APENAS o "papel" do card; texto vem depois das
  // prévias 3D — ver drawWelcomeText, chamado após drawTemplatePreviews).
  const float cardW = 250, cardH = 180;
  const float startX = fbw / 2.f - (cardW * 4 + 40) / 2.f;
  for (int index = 0; index < 4; ++index) {
    const float x = startX + index * (cardW + 10);
    const float y = 280;
    ui_.rect(x, y, cardW, cardH, 1.f, 1.f, 1.f);
    ui_.frame(x, y, cardW, cardH, 3, 0.08f, 0.12f, 0.17f);
    // área da prévia: fundo neutro (a cena 3D é desenhada por cima)
    ui_.rect(x + 16, y + 16, cardW - 32, 66, 0.88f, 0.9f, 0.93f);
    // clique (com consumo — um widget por frame)
    double mx, my;
    mx = static_cast<double>(sf::Mouse::getPosition(*window_).x);
my = static_cast<double>(sf::Mouse::getPosition(*window_).y);
    if (!clickConsumed_ && uiCooldown_ == 0 && buttonHit(mx, my, x, y, cardW, cardH) &&
        sf::Mouse::isButtonPressed(sf::Mouse::Button::Left)) {
      clickConsumed_ = true;
      newFromTemplate(index);
      return;
    }
  }

  // Moldura das prévias por cima da cena 3D (só a borda).
  for (int index = 0; index < 4; ++index) {
    const float x = startX + index * (cardW + 10);
    const float y = 280;
    ui_.frame(x + 16, y + 16, cardW - 32, 66, 2, 0.4f, 0.5f, 0.6f);
  }
}

// Texto dos cards — desenhado DEPOIS das prévias 3D para não ser coberto.
void EditorApp::drawWelcomeText(int fbw, int fbh) {
  const char* names[] = {"Baseplate", "Ilhas Flutuantes", "Obby (Parkour)", "Sala de Exposicao"};
  const char* descs[] = {"piso limpo para comecar do zero", "tres ilhas no ceu com pontes",
                         "plataformas suspensas", "salao com todos os materiais"};
  const float cardW = 250, cardH = 180;
  const float startX = fbw / 2.f - (cardW * 4 + 40) / 2.f;
  for (int index = 0; index < 4; ++index) {
    const float x = startX + index * (cardW + 10);
    const float y = 280;
    uiText(names[index], x + 16, y + 92, 16, 0.08f, 0.12f, 0.17f);
    uiText(descs[index], x + 16, y + 116, 12, 0.35f, 0.4f, 0.46f);
    uiText("[clique para comecar]", x + 16, y + 148, 12, 0.72f, 0.55f, 0.1f);
  }

  // Carregar arquivo
  uiText("ou abra um mapa existente:", fbw / 2.f - font_.textWidth("ou abra um mapa existente:", 14) / 2, 520, 14, 0.3f, 0.35f, 0.42f);
  const float bx = fbw / 2.f - 110, by = 550, bw = 220, bh = 40;
  ui_.rect(bx, by, bw, bh, 0.91f, 0.71f, 0.17f);
  ui_.frame(bx, by, bw, bh, 3, 0.08f, 0.12f, 0.17f);
  uiText("ABRIR .LOOM", bx + (bw - font_.textWidth("ABRIR .LOOM", 16)) / 2, by + 14, 16, 0.15f, 0.12f, 0.0f);
  double mx, my;
  mx = static_cast<double>(sf::Mouse::getPosition(*window_).x);
my = static_cast<double>(sf::Mouse::getPosition(*window_).y);
  const bool down = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
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
  mx = static_cast<double>(sf::Mouse::getPosition(*window_).x);
my = static_cast<double>(sf::Mouse::getPosition(*window_).y);
  if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Left) &&
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
  if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Left) &&
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
  mx = static_cast<double>(sf::Mouse::getPosition(*window_).x);
my = static_cast<double>(sf::Mouse::getPosition(*window_).y);
  const bool down = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
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
          if (saveLoom(filePath_, err)) { addRecent(filePath_); inEditor_ = false; if (!window_->isOpen()) exitRequested_ = true; }
          else setStatus("Erro ao salvar: " + err);
        } else if (saveWithDialog()) {
          inEditor_ = false;
          if (!window_->isOpen()) exitRequested_ = true;
        }
      } else if (std::strcmp(option.label, "Não salvar") == 0) {
        dirty_ = false;
        // Se o pedido veio do X da janela, fecha de verdade.
        if (!window_->isOpen()) exitRequested_ = true;
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
void EditorApp::drawPrefs(int fbw, int fbh) {
  // Popup MODAL no centro da tela: fundo escurecido + painel.
  const float w = 340.f, h = 360.f;
  const float x = (static_cast<float>(fbw) - w) / 2.f;
  const float y = (static_cast<float>(fbh) - h) / 2.f;
  ui_.rect(0, 0, static_cast<float>(fbw), static_cast<float>(fbh), 0.04f, 0.05f, 0.08f, 0.55f);
  ui_.rect(x, y, w, h, th_.panelR, th_.panelG, th_.panelB);
  ui_.frame(x, y, w, h, 2, th_.lineR, th_.lineG, th_.lineB);
  uiText("Preferências do Editor", x + 14, y + 12, 15, th_.inkR, th_.inkG, th_.inkB);
  ui_.rect(x, y + 32, w, 1, th_.lineR, th_.lineG, th_.lineB);

  double mx, my;
  mx = static_cast<double>(sf::Mouse::getPosition(*window_).x);
my = static_cast<double>(sf::Mouse::getPosition(*window_).y);
  const bool down = sf::Mouse::isButtonPressed(sf::Mouse::Button::Left);
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

  // Preview: proporção da tela de teste
  uiText("Proporção do teste:", x + 14, ly, 12, th_.inkR, th_.inkG, th_.inkB);
  ly += 18;
  const char* aspects[] = {"livre", "16:9", "4:3", "1:1"};
  for (int index = 0; index < 4; ++index) {
    const float bx = x + 14 + index * 72.f;
    const bool active = previewAspect_ == index;
    ui_.rect(bx, ly, 66, 20, active ? th_.accentR : th_.panelAltR, active ? th_.accentG : th_.panelAltG, active ? th_.accentB : th_.panelAltB);
    ui_.frame(bx, ly, 66, 20, 1, th_.lineR, th_.lineG, th_.lineB);
    uiText(aspects[index], bx + 14, ly + 5, 11, active ? th_.accentTextR : th_.inkR, active ? th_.accentTextG : th_.inkG, active ? th_.accentTextB : th_.inkB);
    if (clicked && buttonHit(mx, my, bx, ly, 66, 20)) previewAspect_ = index;
  }
  ly += 30;

  uiText("Essas opções não afetam o mapa nem o jogo.", x + 14, ly, 11, th_.inkSoftR, th_.inkSoftG, th_.inkSoftB);

  if (clicked && !(mx > x && mx < x + w && my > y && my < y + h)) {
    prefsOpen_ = false;
    uiCooldown_ = 12;  // o clique de fechar não vaza para a viewport
  }
}

}  // namespace loomia
