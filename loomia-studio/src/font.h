// Texto TTF da UI (Comic Neue, misc/fonts) — rasterizado com FreeType no boot.
//
// Desenho imediato: cada glifo é um quad texturizado. Suporta ASCII e
// Latin-1 (acentos do português via UTF-8).
#pragma once

#include <string>
#include <vector>
#include <cmath>

namespace loomia {

class UiFont {
 public:
  bool init(const std::string& ttfPath, std::string& error);
  void destroy();

  bool ready() const { return ready_; }

  // Medidas (pixels) para o tamanho pedido.
  float lineHeight(float size) const { return size * 1.3f; }
  float textWidth(const std::string& text, float size) const;

  // Chame beginFrame antes de todos os draw, endFrame depois.
  void beginFrame(int fbw, int fbh);
  void endFrame();

  // Desenha o texto (y = topo aproximado da linha). Cores 0..1.
  void draw(const std::string& text, float x, float y, float size, float r, float g, float b, float a = 1.0f);

 private:
  struct Glyph {
    unsigned int texture = 0;
    float w = 0, h = 0;
    float bearingX = 0, bearingY = 0;
    float advance = 0;
  };
  // Cache por tamanho: cada tamanho usado ganha seu próprio conjunto de
  // glifos rasterizados nativamente (sem minificação = sem serrilhado).
  struct SizeSet {
    int pixelSize = 0;
    Glyph ascii[96] = {};
    Glyph latin1[96] = {};
  };
  std::vector<SizeSet> sizeSets_;
  SizeSet* activeSet_ = nullptr;

  const Glyph* glyphFor(unsigned int codepoint) const;
  bool ensureSize(int pixelSize);

  bool ready_ = false;

  // FreeType: mantidos como void* e convertidos no .cpp (o header não inclui
  // ft2build.h para não vazar a dependência).
  void* ftLibrary_ = nullptr;
  void* ftFace_ = nullptr;
  unsigned int vao_ = 0, vbo_ = 0, shader_ = 0;
  int uniformRes_ = -1;
  int uniformSampler_ = -1;
  int width_ = 1, height_ = 1;
};

}  // namespace loomia
