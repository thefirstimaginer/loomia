// Implementação do UiFont (FreeType + quads texturizados).
//
// Simplicidade primeiro: cada glifo é um quad texturizado desenhado na hora
// (`draw` imediato). A UI tem poucas centenas de glifos por frame — custo
// desprezível, e elimina a complexidade de atlas/batch por textura.
#include "font.h"

#include <glad/glad.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <vector>
#include <cstdio>

namespace loomia {

namespace {

const char* FONT_VS = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUv;
layout(location=2) in vec4 aColor;
uniform vec2 uResolution;
out vec2 vUv;
out vec4 vColor;
void main() {
  vec2 p = aPos / uResolution * 2.0 - 1.0;
  gl_Position = vec4(p.x, -p.y, 0.0, 1.0);
  vUv = aUv;
  vColor = aColor;
})";

const char* FONT_FS = R"(#version 330 core
in vec2 vUv;
in vec4 vColor;
uniform sampler2D uText;
out vec4 FragColor;
void main() {
  float alpha = texture(uText, vUv).a;
  FragColor = vec4(vColor.rgb, vColor.a * alpha);
})";

unsigned int compileShader(const char* vsSource, const char* fsSource, std::string& error) {
  auto compile = [&](unsigned int type, const char* source) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    return shader;
  };
  unsigned int vs = compile(GL_VERTEX_SHADER, vsSource);
  unsigned int fs = compile(GL_FRAGMENT_SHADER, fsSource);
  unsigned int program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  int ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  glDeleteShader(vs);
  glDeleteShader(fs);
  if (!ok) {
    error = "shader de fonte não compilou";
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

}  // namespace

// Decodifica um codepoint UTF-8; devolve o tamanho em bytes (1 se inválido).
int decodeUtf8(const std::string& text, size_t pos, unsigned int& out) {
  const unsigned char c0 = static_cast<unsigned char>(text[pos]);
  if (c0 < 0x80) { out = c0; return 1; }
  if ((c0 & 0xE0) == 0xC0 && pos + 1 < text.size()) {
    out = ((c0 & 0x1F) << 6) | (static_cast<unsigned char>(text[pos + 1]) & 0x3F);
    return 2;
  }
  if ((c0 & 0xF0) == 0xE0 && pos + 2 < text.size()) {
    out = ((c0 & 0x0F) << 12) | ((static_cast<unsigned char>(text[pos + 1]) & 0x3F) << 6) |
          (static_cast<unsigned char>(text[pos + 2]) & 0x3F);
    return 3;
  }
  out = '?';
  return 1;
}

bool UiFont::init(const std::string& ttfPath, std::string& error) {
  FT_Library library = nullptr;
  if (FT_Init_FreeType(&library)) {
    error = "freetype init falhou";
    return false;
  }
  FT_Face face = nullptr;
  if (FT_New_Face(library, ttfPath.c_str(), 0, &face)) {
    error = "não consegui abrir a fonte: " + ttfPath;
    FT_Done_FreeType(library);
    return false;
  }

  // Rasteriza a 64px e escala no draw — boa qualidade em qualquer tamanho.
  FT_Set_Pixel_Sizes(face, 0, 64);
  // Rasteriza ASCII e Latin-1 (acentos). Índice: ASCII -> code-32, Latin-1 -> code-0xA1.
  auto rasterize = [&](unsigned int codepoint, Glyph& out) {
        if (FT_Load_Char(face, codepoint, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL)) return;
    const FT_GlyphSlot slot = face->glyph;
    const unsigned int w = slot->bitmap.width;
    const unsigned int h = slot->bitmap.rows;
    out.advance = static_cast<float>(slot->advance.x >> 6);
    out.bearingX = static_cast<float>(slot->bitmap_left);
    out.bearingY = static_cast<float>(slot->bitmap_top);
    if (w == 0 || h == 0) return;
    std::vector<unsigned char> rgba(w * h * 4);
    for (unsigned int pixel = 0; pixel < w * h; ++pixel) {
      rgba[pixel * 4 + 0] = 255;
      rgba[pixel * 4 + 1] = 255;
      rgba[pixel * 4 + 2] = 255;
      rgba[pixel * 4 + 3] = slot->bitmap.buffer[pixel];
    }
    glGenTextures(1, &out.texture);
    glBindTexture(GL_TEXTURE_2D, out.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(w), static_cast<GLsizei>(h), 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    out.w = static_cast<float>(w);
    out.h = static_cast<float>(h);
  };
  for (unsigned int code = 32; code < 128; ++code) rasterize(code, glyphs_[code - 32]);
  for (unsigned int code = 0xA1; code < 0x100; ++code) rasterize(code, latin1_[code - 0xA1]);

  FT_Done_Face(face);
  FT_Done_FreeType(library);

  shader_ = compileShader(FONT_VS, FONT_FS, error);
  if (!shader_) return false;
  uniformRes_ = glGetUniformLocation(shader_, "uResolution");
  uniformSampler_ = glGetUniformLocation(shader_, "uText");
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  ready_ = true;
  return true;
}

void UiFont::destroy() {
  for (Glyph& glyph : glyphs_) {
    if (glyph.texture) glDeleteTextures(1, &glyph.texture);
  }
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (shader_) glDeleteProgram(shader_);
  vao_ = vbo_ = shader_ = 0;
  ready_ = false;
}

const UiFont::Glyph* UiFont::glyphFor(unsigned int codepoint) const {
  if (codepoint >= 32 && codepoint < 128) return &glyphs_[codepoint - 32];
  if (codepoint >= 0xA1 && codepoint < 0x100) return &latin1_[codepoint - 0xA1];
  return nullptr;
}

float UiFont::textWidth(const std::string& text, float size) const {
  if (!ready_) return static_cast<float>(text.size()) * size * 0.55f;
  const float scale = size / 64.f;
  float width = 0;
  for (size_t pos = 0; pos < text.size();) {
    unsigned int code = 0;
    pos += decodeUtf8(text, pos, code);
    const Glyph* glyph = glyphFor(code);
    if (!glyph) { width += 20 * scale; continue; }
    width += glyph->advance * scale;
  }
  return width;
}

void UiFont::beginFrame(int fbw, int fbh) {
  width_ = fbw;
  height_ = fbh;
  if (!ready_) return;
  glUseProgram(shader_);
  glUniform2f(uniformRes_, static_cast<float>(fbw), static_cast<float>(fbh));
  glUniform1i(uniformSampler_, 0);
  glBindVertexArray(vao_);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void UiFont::endFrame() {
  if (!ready_) return;
  glBindVertexArray(0);
  glUseProgram(0);
}

void UiFont::draw(const std::string& text, float x, float y, float size, float r, float g, float b, float a) {
  if (!ready_) return;
  glUseProgram(shader_);
  // uResolution vem da viewport atual — fonte definitiva, sem depender de
  // beginFrame (que ninguém chama).
  GLint viewport[4] = {};
  glGetIntegerv(GL_VIEWPORT, viewport);
  width_ = viewport[2];
  height_ = viewport[3];
  glUniform2f(uniformRes_, static_cast<float>(width_), static_cast<float>(height_));
  glUniform1i(uniformSampler_, 0);

  const float scale = size / 64.f;
  float cursorX = x;
  const float baseline = y + size * 0.85f;  // y = topo aproximado da linha
  glUseProgram(shader_);

  for (size_t pos = 0; pos < text.size();) {
    unsigned int code = 0;
    pos += decodeUtf8(text, pos, code);
    const Glyph* glyphPtr = glyphFor(code);
    if (!glyphPtr) { cursorX += 20 * scale; continue; }
    const Glyph& glyph = *glyphPtr;
    if (!glyph.texture || glyph.w <= 0) {
      cursorX += glyph.advance * scale;
      continue;
    }
    const float left = cursorX + glyph.bearingX * scale;
    const float top = baseline - glyph.bearingY * scale;
    const float w = glyph.w * scale;
    const float h = glyph.h * scale;
    const float quad[6][8] = {
        {left, top, 0, 0, r, g, b, a},
        {left + w, top, 1, 0, r, g, b, a},
        {left + w, top + h, 1, 1, r, g, b, a},
        {left, top, 0, 0, r, g, b, a},
        {left + w, top + h, 1, 1, r, g, b, a},
        {left, top + h, 0, 1, r, g, b, a},
    };
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), reinterpret_cast<void*>(4 * sizeof(float)));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, glyph.texture);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    cursorX += glyph.advance * scale;
  }
}

}  // namespace loomia
