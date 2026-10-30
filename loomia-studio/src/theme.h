// Tema visual do studio (claro/escuro) e métricas de layout responsivo.
//
// Todas as cores vêm daqui — os painéis nunca codificam RGB direto. O tema
// é uma preferência do EDITOR (prefsDark_); não afeta o mapa.
#pragma once

#include <algorithm>
#include <cmath>

namespace loomia {

struct Theme {
  // painéis
  float bgR = 0.94f, bgG = 0.93f, bgB = 0.88f;      // fundo do app
  float panelR = 1.f, panelG = 1.f, panelB = 1.f;   // painéis/cartões
  float panelAltR = 0.97f, panelAltG = 0.97f, panelAltB = 0.99f;
  float lineR = 0.72f, lineG = 0.74f, lineB = 0.78f; // divisórias finas
  float inkR = 0.08f, inkG = 0.12f, inkB = 0.17f;    // texto principal
  float inkSoftR = 0.38f, inkSoftG = 0.43f, inkSoftB = 0.5f;
  float accentR = 0.91f, accentG = 0.71f, accentB = 0.17f;  // dourado Loomia
  float accentTextR = 0.15f, accentTextG = 0.12f, accentTextB = 0.f;
  float selR = 0.2f, selG = 0.5f, selB = 0.95f;      // azul de seleção
  float viewportR = 0.87f, viewportG = 0.9f, viewportB = 0.93f; // fundo cinza-claro
  float gridR = 0.78f, gridG = 0.81f, gridB = 0.85f;
  float dangerR = 0.75f, dangerG = 0.24f, dangerB = 0.18f;

  static Theme light() { return {}; }

  static Theme dark() {
    Theme t;
    t.bgR = 0.09f; t.bgG = 0.1f; t.bgB = 0.12f;
    t.panelR = 0.15f; t.panelG = 0.16f; t.panelB = 0.19f;
    t.panelAltR = 0.18f; t.panelAltG = 0.19f; t.panelAltB = 0.23f;
    t.lineR = 0.28f; t.lineG = 0.3f; t.lineB = 0.34f;
    t.inkR = 0.92f; t.inkG = 0.93f; t.inkB = 0.95f;
    t.inkSoftR = 0.65f; t.inkSoftG = 0.68f; t.inkSoftB = 0.72f;
    t.accentR = 0.93f; t.accentG = 0.75f; t.accentB = 0.25f;
    t.accentTextR = 0.08f; t.accentTextG = 0.06f; t.accentTextB = 0.f;
    t.selR = 0.25f; t.selG = 0.55f; t.selB = 0.95f;
    t.viewportR = 0.13f; t.viewportG = 0.14f; t.viewportB = 0.17f;
    t.gridR = 0.22f; t.gridG = 0.23f; t.gridB = 0.27f;
    t.dangerR = 0.9f; t.dangerG = 0.4f; t.dangerB = 0.35f;
    return t;
  }
};

// Áreas do layout (todas derivadas do tamanho do framebuffer — nada fixo).
struct LayoutRects {
  int fbw = 0, fbh = 0;
  int topbarH = 46;      // barra superior (menus + ícones)
  int tabsH = 30;        // faixa de abas Cena/Scripts + Play
  int leftW = 44;        // sidebar de ferramentas (ícones verticais)
  int rightW = 280;      // hierarquia + propriedades (redimensionável)
  int bottomH = 118 + 26; // faixa de recursos + linha de status

  int viewportX() const { return leftW; }
  int viewportY() const { return topbarH + tabsH; }
  int viewportW() const { return std::max(120, fbw - leftW - rightW); }
  int viewportH() const { return std::max(120, fbh - topbarH - tabsH - bottomH); }
};

constexpr int MIN_WINDOW_W = 1024;
constexpr int MIN_WINDOW_H = 640;

}  // namespace loomia
