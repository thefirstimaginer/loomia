// Implementação do IconAtlas: texturas GL a partir de PNGs.
//
// O draw usa um shader dedicado (a textura por quad, estilo do UiFont) —
// simples e suficiente para poucos ícones por frame.
#include "icon_atlas.h"
#include "png_loader.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>

namespace loomia {

namespace {
const char* ICON_VS = R"(#version 330 core
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

const char* ICON_FS = R"(#version 330 core
in vec2 vUv;
in vec4 vColor;
uniform sampler2D uIcon;
out vec4 FragColor;
void main() {
  FragColor = texture(uIcon, vUv) * vColor;
})";

unsigned int compileIconShader(const char* vs, const char* fs, std::string& error) {
  auto compile = [](unsigned int type, const char* source) {
    unsigned int shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    return shader;
  };
  unsigned int v = compile(GL_VERTEX_SHADER, vs);
  unsigned int f = compile(GL_FRAGMENT_SHADER, fs);
  unsigned int program = glCreateProgram();
  glAttachShader(program, v);
  glAttachShader(program, f);
  glLinkProgram(program);
  int ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  glDeleteShader(v);
  glDeleteShader(f);
  if (!ok) {
    error = "shader de ícone não compilou";
    return 0;
  }
  return program;
}

}  // namespace

bool IconAtlas::init(const std::string& iconsDir, std::string& error) {
  shader_ = compileIconShader(ICON_VS, ICON_FS, error);
  if (!shader_) return false;
  uniformRes_ = glGetUniformLocation(shader_, "uResolution");
  uniformSampler_ = glGetUniformLocation(shader_, "uIcon");
  glGenVertexArrays(1, &vao_);
  glGenBuffers(1, &vbo_);

  // Arquivos reais em misc/icons/loomia-studio-icons e seus apelidos na UI.
  const std::pair<const char*, const char*> files[] = {
      {"icons8-cursor-50.png", "select"},
      {"icons8-edit-pencil-50.png", "add"},
      {"icons8-move-50.png", "move"},
      {"icons8-resize-50.png", "scale"},
      {"icons8-pause-50.png", "stop"},
      {"icons8-lua-language-50.png", "lua"},
      {"icons8-save-50.png", "save"},
      {"icons8-sun-50.png", "theme"},
      {"icons8-do-not-disturb-ios-50.png", "prefs"},
  };
  for (const auto& entry : files) {
    const std::string path = iconsDir + "/" + entry.first;
    std::string pngError;
    PngImage image = loadPngFile(path, pngError);
    if (!image.ok) continue;  // ícone ausente: o fallback vetorial cobre
    const std::string& name = entry.second;

    Icon icon;
    icon.width = image.width;
    icon.height = image.height;
    glGenTextures(1, &icon.texture);
    glBindTexture(GL_TEXTURE_2D, icon.texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenerateMipmap(GL_TEXTURE_2D);
    textures_[name] = icon;
  }
  if (textures_.empty()) {
    error = "nenhum ícone carregado de " + iconsDir;
    return false;
  }
  ready_ = true;
  return true;
}

void IconAtlas::destroy() {
  for (auto& entry : textures_) glDeleteTextures(1, &entry.second.texture);
  textures_.clear();
  if (vbo_) glDeleteBuffers(1, &vbo_);
  if (vao_) glDeleteVertexArrays(1, &vao_);
  if (shader_) glDeleteProgram(shader_);
  vao_ = vbo_ = shader_ = 0;
  ready_ = false;
}

void IconAtlas::draw(class UiOverlay& ui, const std::string& name, float x, float y, float size, float alpha) const {
  const auto it = textures_.find(name);
  if (it == textures_.end()) return;
  const Icon& icon = it->second;
  if (!icon.texture) return;

  GLint viewport[4] = {};
  glGetIntegerv(GL_VIEWPORT, viewport);
  const float w = static_cast<float>(viewport[2]), h = static_cast<float>(viewport[3]);

  glUseProgram(shader_);
  glUniform2f(uniformRes_, w, h);
  glUniform1i(uniformSampler_, 0);

  // Quad mantendo o aspecto do PNG dentro do quadrado pedido.
  const float aspect = static_cast<float>(icon.width) / static_cast<float>(icon.height);
  float qw = size, qh = size;
  if (aspect > 1) qh = size / aspect; else qw = size * aspect;
  const float left = x + (size - qw) / 2, top = y + (size - qh) / 2;

  const float quad[6][8] = {
      {left, top, 0, 0, 1, 1, 1, alpha},
      {left + qw, top, 1, 0, 1, 1, 1, alpha},
      {left + qw, top + qh, 1, 1, 1, 1, 1, alpha},
      {left, top, 0, 0, 1, 1, 1, alpha},
      {left + qw, top + qh, 1, 1, 1, 1, 1, alpha},
      {left, top + qh, 0, 1, 1, 1, 1, alpha},
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
  glBindTexture(GL_TEXTURE_2D, icon.texture);
  glDisable(GL_DEPTH_TEST);
  glDrawArrays(GL_TRIANGLES, 0, 6);
  glEnable(GL_DEPTH_TEST);
  glBindVertexArray(0);
}

}  // namespace loomia
