#include "labels.h"

#include <glad/glad.h>

#include <array>
#include <cctype>
#include <cstdio>

namespace loomia {

namespace {

// Fonte bitmap 5x7 (maiúsculas, dígitos e pontuação). Cada glifo tem 7 linhas
// de 5 bits; o bit 4 é a coluna da esquerda. Suporta acentos comuns virando
// a letra base (ex.: "Á" -> "A"), o que basta para nomes de jogador.
struct Glyph {
  char code;
  std::array<uint8_t, 7> rows;
};

constexpr Glyph kGlyphs[] = {
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
    {'-', {0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00}},
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
};

const Glyph* findGlyph(char code) {
  code = static_cast<char>(std::toupper(static_cast<unsigned char>(code)));
  for (const Glyph& glyph : kGlyphs) {
    if (glyph.code == code) return &glyph;
  }
  return findGlyph('?');
}

const char* LABEL_VERTEX = R"GLSL(
#version 330 core
layout(location = 0) in vec2 aPosition;   // pixels de tela (origem no canto SUP. esq.)
uniform vec2 uViewport;
uniform vec2 uOffset;
void main() {
  vec2 screen = aPosition + uOffset;
  vec2 ndc = vec2(screen.x / uViewport.x * 2.0 - 1.0,
                  1.0 - screen.y / uViewport.y * 2.0);
  gl_Position = vec4(ndc, 0.0, 1.0);
}
)GLSL";

const char* LABEL_FRAGMENT = R"GLSL(
#version 330 core
uniform vec4 uColor;
out vec4 fragColor;
void main() {
  fragColor = uColor;
}
)GLSL";

unsigned int compileStage(unsigned int type, const char* source) {
  const unsigned int shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  int ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024] = {0};
    glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
    std::fprintf(stderr, "[loomia] shader de etiqueta: %s\n", log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

}  // namespace

bool LabelRenderer::init(std::string& error) {
  const unsigned int vertex = compileStage(GL_VERTEX_SHADER, LABEL_VERTEX);
  const unsigned int fragment = compileStage(GL_FRAGMENT_SHADER, LABEL_FRAGMENT);
  if (!vertex || !fragment) {
    error = "não consegui compilar o shader das etiquetas";
    return false;
  }
  program_ = glCreateProgram();
  glAttachShader(program_, vertex);
  glAttachShader(program_, fragment);
  glLinkProgram(program_);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  int ok = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &ok);
  if (!ok) {
    error = "não consegui linkar o shader das etiquetas";
    return false;
  }
  uniformViewport_ = glGetUniformLocation(program_, "uViewport");
  uniformColor_ = glGetUniformLocation(program_, "uColor");
  uniformOffset_ = glGetUniformLocation(program_, "uOffset");

  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);
  glBindVertexArray(vao_);
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, nullptr);
  glBindVertexArray(0);
  return true;
}

void LabelRenderer::destroy() {
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (program_) glDeleteProgram(program_);
  vbo_ = vao_ = program_ = 0;
}

void LabelRenderer::begin(int viewportWidth, int viewportHeight) {
  viewportWidth_ = static_cast<float>(viewportWidth);
  viewportHeight_ = static_cast<float>(viewportHeight);
  if (!program_) return;
  glUseProgram(program_);
  glUniform2f(uniformViewport_, viewportWidth_, viewportHeight_);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glBindVertexArray(vao_);
}

void LabelRenderer::end() {
  glBindVertexArray(0);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);
}

void LabelRenderer::drawQuad(float x, float y, float width, float height, const Color3& color,
                             float alpha) {
  const float vertices[8] = {x, y, x + width, y, x, y + height, x + width, y + height};
  glBindBuffer(GL_ARRAY_BUFFER, vbo_);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
  glUniform2f(uniformOffset_, 0.f, 0.f);
  glUniform4f(uniformColor_, color.rf(), color.gf(), color.bf(), alpha);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void LabelRenderer::drawCentered(float screenX, float screenY, const std::string& text,
                                 const Color3& color, float scale, float alpha) {
  if (!program_ || text.empty()) return;
  const float glyphWidth = 5.f * scale;
  const float glyphHeight = 7.f * scale;
  const float advance = glyphWidth + scale;
  const float totalWidth = static_cast<float>(text.size()) * advance - scale;

  // Fundo semitransparente (melhora a leitura sobre qualquer bloco).
  const float padding = scale * 2.f;
  drawQuad(screenX - totalWidth / 2 - padding, screenY - glyphHeight / 2 - padding,
           totalWidth + padding * 2, glyphHeight + padding * 2, Color3{10, 16, 28}, alpha * 0.55f);

  float penX = screenX - totalWidth / 2;
  const float top = screenY - glyphHeight / 2;
  for (char code : text) {
    const Glyph* glyph = findGlyph(code);
    for (int row = 0; row < 7; row++) {
      const uint8_t bits = glyph->rows[static_cast<size_t>(row)];
      for (int column = 0; column < 5; column++) {
        if (!(bits & (1 << (4 - column)))) continue;
        drawQuad(penX + static_cast<float>(column) * scale,
                 top + static_cast<float>(row) * scale, scale, scale, color, alpha);
      }
    }
    penX += advance;
  }
}

bool LabelRenderer::project(const Mat4& viewProjection, const Vec3& world, int width, int height,
                            float& screenX, float& screenY) {
  const float* m = viewProjection.m;
  // Coluna-major: x' = m0*x + m4*y + m8*z + m12, etc.
  const float clipX = m[0] * world.x + m[4] * world.y + m[8] * world.z + m[12];
  const float clipY = m[1] * world.x + m[5] * world.y + m[9] * world.z + m[13];
  const float clipW = m[3] * world.x + m[7] * world.y + m[11] * world.z + m[15];
  if (clipW <= 0.001f) return false;
  const float ndcX = clipX / clipW;
  const float ndcY = clipY / clipW;
  screenX = (ndcX * 0.5f + 0.5f) * static_cast<float>(width);
  screenY = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(height);
  return true;
}

}  // namespace loomia
