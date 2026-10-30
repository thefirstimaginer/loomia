// Texto 2D sobre o mundo OpenGL (etiquetas de nome dos jogadores).
//
// O jogo desenha em OpenGL puro (janela SFML), então não há texto do SFML na mesma
// janela. Cada etiqueta é feita de glifos bitmap (5x7) desenhados como quads:
// leve, sem atlas de textura e idêntico em qualquer GPU/plataforma.
#pragma once

#include <string>

#include "math.h"

namespace loomia {

class LabelRenderer {
 public:
  bool init(std::string& error);
  void destroy();

  /** Prepara o lote para o frame (desliga depth test e liga blend). */
  void begin(int viewportWidth, int viewportHeight);
  void end();

  /**
   * Desenha `text` centrado em (screenX, screenY) — pixels de tela, origem no
   * canto superior esquerdo. Desenha também um fundo semitransparente.
   */
  void drawCentered(float screenX, float screenY, const std::string& text, const Color3& color,
                    float scale, float alpha);

  /**
   * Projeta uma posição do mundo para a tela. Devolve false quando a posição
   * está atrás da câmera.
   */
  static bool project(const Mat4& viewProjection, const Vec3& world, int width, int height,
                      float& screenX, float& screenY);

 private:
  void drawQuad(float x, float y, float width, float height, const Color3& color, float alpha);

  unsigned int vao_ = 0;
  unsigned int vbo_ = 0;
  unsigned int program_ = 0;
  int uniformViewport_ = -1;
  int uniformColor_ = -1;
  int uniformOffset_ = -1;
  float viewportWidth_ = 1;
  float viewportHeight_ = 1;
};

}  // namespace loomia
