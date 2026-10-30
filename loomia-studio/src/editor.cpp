// Loomia Studio: editor visual de mapas .loom (peça principal).
//
// Reusa a engine do cliente: WorldRenderer (OpenGL 3.3), Vec3/Mat4 e o
// formato .loom (world.h). A UI é desenhada com overlay 2D próprio — sem
// toolkit externo, igual ao cliente.
#include "editor.h"
#include "templates.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "font.h"
#include "renderer.h"
#include "world.h"

#include <cctype>
#include <unistd.h>

namespace loomia {

namespace {

// ---------------------------------------------------------------- overlays 2D

// Shader 2D trivial: posições em pixels + cor por vértice.
const char* UI_VS = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec4 aColor;
uniform vec2 uResolution;
out vec4 vColor;
void main() {
  vec2 p = aPos / uResolution * 2.0 - 1.0;
  gl_Position = vec4(p.x, -p.y, 0.0, 1.0);
  vColor = aColor;
})";

const char* UI_FS = R"(#version 330 core
in vec4 vColor;
out vec4 FragColor;
void main() { FragColor = vColor; })";

// Texto: sem freetype — desenha com uma fonte bitmap 5x7 embutida (estilo
// clássico, legível e zero dependência). Cada glifo = 5x7 células.
struct Glyph5x7 { const char* rows[7]; };

const Glyph5x7 kFont[96] = {
  // 32..126 (ASCII). Espaço = vazio.
  {"     ","     ","     ","     ","     ","     ","     "}, // 32 space
  {"  X  ","  X  ","  X  ","  X  ","  X  ","     ","  X  "}, // 33 !
  {" X X "," X X ","     ","     ","     ","     ","     "}, // 34 "
  {" X X "," X X ","XXXXX"," X X ","XXXXX"," X X "," X X "}, // 35 #
  {"  XX "," X   ","  X  ","   X ","  X  "," X   ","  X  "}, // 36 $
  {" X  X","  X  "," X   ","  X  ","   X "," X  X","     "}, // 37 %
  {"  X  "," X X ","  X  "," X X ","X   X","     ","     "}, // 38 &
  {"  X  ","  X  ","     ","     ","     ","     ","     "}, // 39 '
  {"  XX "," X   "," X   "," X   ","  XX ","     ","     "}, // 40 (
  {" XX  ","   X ","   X ","   X "," XX  ","     ","     "}, // 41 )
  {"     ","X   X"," X X ","  X  "," X X ","X   X","     "}, // 42 *
  {"     ","  X  "," X X ","XXXXX"," X X ","  X  ","     "}, // 43 +
  {"     ","     ","     ","     ","  XX "," X   ","     "}, // 44 ,
  {"     ","     ","XXXXX","     ","     ","     ","     "}, // 45 -
  {"     ","     ","     ","     ","  XX ","  XX ","     "}, // 46 .
  {"    X","   X ","  X  "," X   ","X    ","     ","     "}, // 47 /
  {" XXX ","X   X","X  XX","X X X","XX  X","X   X"," XXX "}, // 48 0
  {"  X  "," XX  ","  X  ","  X  ","  X  ","  X  "," XXX "}, // 49 1
  {" XXX ","X   X","    X","   X ","  X  "," X   ","XXXXX"}, // 50 2
  {"XXXXX","   X ","  X  ","   X ","    X","X   X"," XXX "}, // 51 3
  {"   X ","  XX "," X X ","X  X ","XXXXX","   X ","   X "}, // 52 4
  {"XXXXX","X    ","XXXX ","    X","    X","X   X"," XXX "}, // 53 5
  {"  XX "," X   ","X    ","XXXX ","X   X","X   X"," XXX "}, // 54 6
  {"XXXXX","    X","   X ","  X  "," X   "," X   "," X   "}, // 55 7
  {" XXX ","X   X","X   X"," XXX ","X   X","X   X"," XXX "}, // 56 8
  {" XXX ","X   X","X   X"," XXXX","    X","   X "," XX  "}, // 57 9
  {"     ","  XX ","  X  ","     ","  X  ","  XX ","     "}, // 58 :
  {"     ","  X  ","     ","     ","  X  ","  X  ","     "}, // 59 ;
  {"     ","   X ","  X  "," X   ","  X  ","   X ","     "}, // 60 <
  {"     ","XXXXX","     ","XXXXX","     ","     ","     "}, // 61 =
  {"     ","X    "," X   ","  X  "," X   ","X    ","     "}, // 62 >
  {" XXX ","X   X","    X","   X ","  X  ","     ","  X  "}, // 63 ?
  {" XXX ","X   X","X  XX","X X X","XXXX "," X   "," XX  "}, // 64 @
  {" XXX ","X   X","X   X","XXXXX","X   X","X   X","X   X"}, // 65 A
  {"XXXX ","X   X","X   X","XXXX ","X   X","X   X","XXXX "}, // 66 B
  {" XXX ","X   X","X    ","X    ","X    ","X   X"," XXX "}, // 67 C
  {"XXXX ","X   X","X   X","X   X","X   X","X   X","XXXX "}, // 68 D
  {"XXXXX","X    ","X    ","XXXX ","X    ","X    ","XXXXX"}, // 69 E
  {"XXXXX","X    ","X    ","XXXX ","X    ","X    ","X    "}, // 70 F
  {" XXX ","X   X","X    ","X XXX","X   X","X   X"," XXXX"}, // 71 G
  {"X   X","X   X","X   X","XXXXX","X   X","X   X","X   X"}, // 72 H
  {" XXX ","  X  ","  X  ","  X  ","  X  ","  X  "," XXX "}, // 73 I
  {"  XXX","   X ","   X ","   X ","   X ","X  X "," XX  "}, // 74 J
  {"X   X","X  X ","X X  ","XX   ","X X  ","X  X ","X   X"}, // 75 K
  {"X    ","X    ","X    ","X    ","X    ","X    ","XXXXX"}, // 76 L
  {"X   X","XX XX","X X X","X X X","X   X","X   X","X   X"}, // 77 M
  {"X   X","XX  X","X X X","X  XX","X   X","X   X","X   X"}, // 78 N
  {" XXX ","X   X","X   X","X   X","X   X","X   X"," XXX "}, // 79 O
  {"XXXX ","X   X","X   X","XXXX ","X    ","X    ","X    "}, // 80 P
  {" XXX ","X   X","X   X","X   X","X X X","X  X "," XXXX"}, // 81 Q
  {"XXXX ","X   X","X   X","XXXX ","X X  ","X  X ","X   X"}, // 82 R
  {" XXX ","X   X"," X   ","  X  ","   X ","X   X"," XXX "}, // 83 S
  {"XXXXX","  X  ","  X  ","  X  ","  X  ","  X  ","  X  "}, // 84 T
  {"X   X","X   X","X   X","X   X","X   X","X   X"," XXX "}, // 85 U
  {"X   X","X   X","X   X","X   X","X   X"," X X ","  X  "}, // 86 V
  {"X   X","X   X","X   X","X X X","X X X","XX XX","X   X"}, // 87 W
  {"X   X","X   X"," X X ","  X  "," X X ","X   X","X   X"}, // 88 X
  {"X   X","X   X"," X X ","  X  ","  X  ","  X  ","  X  "}, // 89 Y
  {"XXXXX","    X","   X ","  X  "," X   ","X    ","XXXXX"}, // 90 Z
  {" XXX "," X   "," X   "," X   "," X   "," X   "," XXX "}, // 91 [
  {"X    "," X   ","  X  ","  X  ","  X  ","   X ","    X"}, // 92 backslash
  {" XXX ","   X ","   X ","   X ","   X ","   X "," XXX "}, // 93 ]
  {"  X  "," X X ","X   X","     ","     ","     ","     "}, // 94 ^
  {"     ","     ","     ","     ","     ","XXXXX","     "}, // 95 _
  {" X   ","  X  ","   X ","     ","     ","     ","     "}, // 96 `
  {"     ","  XX "," X   "," XXX ","X   X","X   X"," XXX "}, // 97 a
  {"X    ","X    "," XXX ","X   X","X   X","X   X"," XXX "}, // 98 b
  {"     "," XXX ","X    ","X    ","X    ","X   X"," XXX "}, // 99 c
  {"    X","    X"," XXX ","X   X","X   X","X   X"," XXX "}, // 100 d
  {"     "," XXX ","X   X","XXXXX","X    ","X   X"," XXX "}, // 101 e
  {"  XX "," X   ","XXXX "," X   "," X   "," X   "," X   "}, // 102 f
  {"     "," XXX ","X   X","X   X"," XXXX","    X"," XXXX"}, // 103 g
  {"X    ","X    "," XXX ","X  X ","X  X ","X  X ","X  X "}, // 104 h
  {"  X  ","     "," XX  ","  X  ","  X  ","  X  "," XXX "}, // 105 i
  {"   X ","     ","   X ","   X ","   X ","X  X "," XX  "}, // 106 j
  {"X    ","X    ","X  X ","X X  ","XX   ","X X  ","X  X "}, // 107 k
  {" XX  ","  X  ","  X  ","  X  ","  X  ","  X  "," XXX "}, // 108 l
  {"     ","XX X ","X X X","X X X","X X X","X X X","X   X"}, // 109 m
  {"     "," XXX ","X   X","X   X","X   X","X   X","X   X"}, // 110 n
  {"     "," XXX ","X   X","X   X","X   X","X   X"," XXX "}, // 111 o
  {"     ","XXXX ","X   X","X   X","XXXX ","X    ","X    "}, // 112 p
  {"     "," XXX ","X   X","X   X"," XXX ","    X","    X"}, // 113 q
  {"     "," XXX ","X   X"," X   "," X   ","     "," X   "}, // 114 r
  {"     "," XXXX","X    "," XXX ","    X","X   X"," XXX "}, // 115 s
  {" X   ","XXXX "," X   "," X   "," X   "," XX  ","     "}, // 116 t
  {"     ","X   X","X   X","X   X","X   X","X   X"," XXXX"}, // 117 u
  {"     ","X   X","X   X"," X X "," X X ","  X  ","  X  "}, // 118 v
  {"     ","X   X","X   X","X X X","X X X","X X X"," X X "}, // 119 w
  {"     ","X   X"," X X ","  X  "," X X ","X   X","     "}, // 120 x
  {"     ","X   X","X   X"," XXXX","    X"," XXX ","    X"}, // 121 y
  {"     ","XXXXX","   X ","  X  "," X   ","X    ","XXXXX"}, // 122 z
  {"  X  "," X X ","X   X","X   X","X   X"," X X ","  X  "}, // 123 {
  {"  X  ","  X  "," X X ","X   X"," X X ","  X  ","  X  "}, // 124 |
  {"   X ","  X  "," X X ","X   X"," X X ","  X  ","   X "}, // 125 }
  {" XXX ","X   X","   X ","  X  ","   X ","     ","  X  "}, // 126 ~
};

// Desenho de overlay: acumula triângulos em CPU e desenha com um VBO dinâmico.
}  // namespace (anon)

// Interseção raio- AABB; devolve a normal da face atingida.
bool rayAabb(const Vec3& origin, const Vec3& dir, const Vec3& bmin, const Vec3& bmax, float& outT, Vec3& outNormal) {
  float tmin = -1e9f, tmax = 1e9f;
  int axis = 0, sign = 0;
  const float* o = &origin.x;
  const float* d = &dir.x;
  const float* mn = &bmin.x;
  const float* mx = &bmax.x;
  for (int index = 0; index < 3; ++index) {
    if (std::fabs(d[index]) < 1e-9f) {
      if (o[index] < mn[index] || o[index] > mx[index]) return false;
      continue;
    }
    const float inv = 1.0f / d[index];
    float t0 = (mn[index] - o[index]) * inv;
    float t1 = (mx[index] - o[index]) * inv;
    int faceSign = -1;
    if (t0 > t1) { std::swap(t0, t1); faceSign = 1; }
    if (t0 > tmin) { tmin = t0; axis = index; sign = faceSign; }
    if (t1 < tmax) tmax = t1;
    if (tmin > tmax) return false;
  }
  if (tmax < 0) return false;
  outT = tmin > 0 ? tmin : tmax;
  outNormal = {0, 0, 0};
  reinterpret_cast<float*>(&outNormal)[axis] = static_cast<float>(sign) * (tmin > 0 ? -1.f : 1.f);
  return true;
}

// Interseção raio-plano y = planeY (para o chão de referência).
bool rayPlaneY(const Vec3& origin, const Vec3& dir, float planeY, float& outT) {
  if (std::fabs(dir.y) < 1e-9f) return false;
  const float t = (planeY - origin.y) / dir.y;
  if (t <= 0) return false;
  outT = t;
  return true;
}
// ---- UiOverlay: implementação (compartilhada com editor_ui.cpp) ----

bool UiOverlay::init(std::string& error) {
  if (!shader.compile(UI_VS, UI_FS, error)) return false;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  ready = true;
  return true;
}

void UiOverlay::destroy() {
  if (vbo) glDeleteBuffers(1, &vbo);
  if (vao) glDeleteVertexArrays(1, &vao);
  shader.destroy();
  ready = false;
}

void UiOverlay::tri(float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a) {
  verts.insert(verts.end(), {x0, y0, r, g, b, a, x1, y1, r, g, b, a, x2, y2, r, g, b, a});
}

void UiOverlay::rect(float x, float y, float w, float h, float r, float g, float b, float a) {
  tri(x, y, x + w, y, x + w, y + h, r, g, b, a);
  tri(x, y, x + w, y + h, x, y + h, r, g, b, a);
}

void UiOverlay::frame(float x, float y, float w, float h, float t, float r, float g, float b, float a) {
  rect(x, y, w, t, r, g, b, a);
  rect(x, y + h - t, w, t, r, g, b, a);
  rect(x, y, t, h, r, g, b, a);
  rect(x + w - t, y, t, h, r, g, b, a);
}

void UiOverlay::text(const std::string& str, float x, float y, float cell, float r, float g, float b, float a) {
  float cx = x;
  for (char ch : str) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (ch == '\n') { y += cell * 8; cx = x; continue; }
    if (c < 32 || c > 126) { cx += cell * 6; continue; }
    const Glyph5x7& glyph = kFont[c - 32];
    if (!glyph.rows[0]) { cx += cell * 6; continue; }
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if (glyph.rows[row][col] != 'X') continue;
        rect(cx + col * cell, y + row * cell, cell, cell, r, g, b, a);
      }
    }
    cx += cell * 6;
  }
}

float UiOverlay::textWidth(const std::string& str, float cell) const {
  return static_cast<float>(str.size()) * cell * 6;
}

void UiOverlay::flush() {
  if (!ready || verts.empty()) return;
  shader.use();
  const int res = shader.uniform("uResolution");
  if (res >= 0) glUniform2f(res, static_cast<float>(width), static_cast<float>(height));
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(float)), verts.data(), GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(0));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 6 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
  glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 6));
  glBindVertexArray(0);
  verts.clear();
}

// ============================================================================
// EditorApp
// ============================================================================


EditorApp::EditorApp() {
  // Estado inicial razoável (definido também no header por segurança).
  status_ = "Bem-vindo ao Loomia Studio!";
}

EditorApp::~EditorApp() { shutdown(); }

EditorResult runStudio() {
  EditorApp app;
  return app.run();
}

// ------------------------------------------------------------------ init

bool EditorApp::initWindow(std::string& error) {
  if (!glfwInit()) {
    error = "não consegui iniciar o GLFW";
    return false;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

  window_ = glfwCreateWindow(WINDOW_W, WINDOW_H, "Loomia Studio", nullptr, nullptr);
  glfwSetWindowSizeLimits(window_, MIN_WINDOW_W, MIN_WINDOW_H, GLFW_DONT_CARE, GLFW_DONT_CARE);
  if (!window_) {
    error = "não consegui criar a janela OpenGL 3.3 (a GPU suporta?)";
    glfwTerminate();
    return false;
  }
  glfwMakeContextCurrent(window_);
  glfwSwapInterval(1);
  glfwSetWindowUserPointer(window_, this);

  glfwSetKeyCallback(window_, [](GLFWwindow* w, int key, int, int action, int) {
    if (auto* self = static_cast<EditorApp*>(glfwGetWindowUserPointer(w)); self && key >= 0 && key < 512) {
      self->keys_[key] = action != GLFW_RELEASE;
    }
  });
  glfwSetScrollCallback(window_, [](GLFWwindow* w, double, double yoff) {
    if (auto* self = static_cast<EditorApp*>(glfwGetWindowUserPointer(w))) {
      const LayoutRects lay = self->layout();
      double cx = 0, cy = 0;
      glfwGetCursorPos(w, &cx, &cy);
      const bool inside = cx >= lay.viewportX() && cx <= lay.viewportX() + lay.viewportW() &&
                          cy >= lay.viewportY() && cy <= lay.viewportY() + lay.viewportH();
      if (inside) self->camDist_ = std::clamp(self->camDist_ - static_cast<float>(yoff) * 2.5f, 4.f, 220.f);
    }
  });
  glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* w, int width, int height) {
    if (auto* self = static_cast<EditorApp*>(glfwGetWindowUserPointer(w))) {
      self->fbw_ = width > 0 ? width : 1;
      self->fbh_ = height > 0 ? height : 1;
    }
  });

  if (!renderer_.init(error)) return false;
  if (!ui_.init(error)) return false;
  // Fonte da UI: Comic Neue de misc/fonts. Os caminhos são resolvidos a
  // partir do executável (/proc/self/exe) e da env LOOMIA_STUDIO_ROOT, então
  // funciona no build/, no AppImage e em qualquer cwd.
  std::vector<std::string> fontCandidates;
  if (const char* root = std::getenv("LOOMIA_STUDIO_ROOT")) {
    fontCandidates.push_back(std::string(root) + "/misc/fonts/Comic_Neue/ComicNeue-Bold.ttf");
  }
  {
    char exe[4096] = {};
    const ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len > 0) {
      std::string exeDir(exe, static_cast<size_t>(len));
      // exeDir = .../loomia-studio/build (ou onde o binário estiver)
      for (int up = 0; up <= 3; ++up) {
        std::string base = exeDir;
        for (int step = 0; step < up; ++step) base += "/..";
        fontCandidates.push_back(base + "/misc/fonts/Comic_Neue/ComicNeue-Bold.ttf");
        fontCandidates.push_back(base + "/../misc/fonts/Comic_Neue/ComicNeue-Bold.ttf");
      }
    }
  }
  for (const std::string& candidate : fontCandidates) {
    std::string fontError;
    if (font_.init(candidate, fontError)) break;
    fontError_ = fontError;
  }
  // Renderer das prévias dos templates (contexto já é o corrente).
  loadRecents();
  std::string previewError;
  // Ícones PNG: misc/icons/loomia-studio-icons. Resolve pelo LOOMIA_STUDIO_ROOT,
  // subindo diretórios a partir do executável até achar a pasta.
  {
    std::vector<std::string> candidates;
    if (const char* root = std::getenv("LOOMIA_STUDIO_ROOT")) {
      candidates.push_back(std::string(root) + "/misc/icons/loomia-studio-icons");
      candidates.push_back(std::string(root) + "/../misc/icons/loomia-studio-icons");
    }
    char exe[4096] = {};
    const ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (len > 0) {
      std::string base(exe, static_cast<size_t>(len));
      // Sobe até 6 níveis, testando "misc/..." em cada um.
      for (int up = 0; up <= 6; ++up) {
        const size_t slash = base.find_last_of('/');
        if (slash == std::string::npos) break;
        base = base.substr(0, slash);
        candidates.push_back(base + "/misc/icons/loomia-studio-icons");
      }
    }
    candidates.push_back("misc/icons/loomia-studio-icons");
    candidates.push_back("../misc/icons/loomia-studio-icons");
    std::string iconError;
    for (const std::string& candidate : candidates) {
      if (icons_.init(candidate, iconError)) {
        iconsDir_ = candidate;
        break;
      }
    }
  }

  previewReady_ = previewRenderer_.init(previewError);
  return true;
}

void EditorApp::shutdown() {
  // Callbacks primeiro: nunca podem disparar com o objeto já destruído
  // (era a causa do segfault ao fechar a janela).
  if (window_) {
    glfwSetWindowUserPointer(window_, nullptr);
    glfwSetKeyCallback(window_, nullptr);
    glfwSetScrollCallback(window_, nullptr);
    glfwSetFramebufferSizeCallback(window_, nullptr);
  }
  if (window_) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
  glfwTerminate();
}

void EditorApp::setStatus(const std::string& message) { status_ = message; }

void EditorApp::markDirty() { dirty_ = true; }

void EditorApp::pushUndo() {
  undoStack_.push_back(world_);
  if (undoStack_.size() > 64) undoStack_.erase(undoStack_.begin());
  redoStack_.clear();
}

void EditorApp::undo() {
  if (undoStack_.empty()) { setStatus("Nada para desfazer."); return; }
  redoStack_.push_back(world_);
  world_ = undoStack_.back();
  undoStack_.pop_back();
  syncBlockMeta();
  const int count = static_cast<int>(world_.blocks.size());
  if (selected_ >= count) selected_ = -1;
  // A seleção múltipla pode apontar para índices que deixaram de existir.
  selection_.erase(std::remove_if(selection_.begin(), selection_.end(),
                                  [count](int index) { return index < 0 || index >= count; }),
                   selection_.end());
  renderer_.upload(world_);
  dirty_ = true;
  setStatus("Desfeito.");
}

void EditorApp::redo() {
  if (redoStack_.empty()) { setStatus("Nada para refazer."); return; }
  undoStack_.push_back(world_);
  world_ = redoStack_.back();
  redoStack_.pop_back();
  syncBlockMeta();
  const int count = static_cast<int>(world_.blocks.size());
  if (selected_ >= count) selected_ = -1;
  selection_.erase(std::remove_if(selection_.begin(), selection_.end(),
                                  [count](int index) { return index < 0 || index >= count; }),
                   selection_.end());
  renderer_.upload(world_);
  dirty_ = true;
  setStatus("Refeito.");
}

// ------------------------------------------------------------------ arquivo

void EditorApp::updateCamera(float dt) {
  (void)dt;
  // Zoom com a roda
  const double scroll = 0;  // preenchido no loop via glfwGetWindowScroll? (abaixo)
  (void)scroll;
  const Vec3 eye{
      camTarget_.x + camDist_ * std::cos(camPitch_) * std::sin(camYaw_),
      camTarget_.y + camDist_ * std::sin(camPitch_),
      camTarget_.z + camDist_ * std::cos(camPitch_) * std::cos(camYaw_)};
  // A câmera é reconstruída no draw (view matrix por frame).
}

RayHit EditorApp::pickBlock(double mouseX, double mouseY, int fbw, int fbh) {
  RayHit result;
  // O raio é construído no espaço do VIEWPORT (painéis não fazem parte).
  const LayoutRects lay = layout();
  const float vw = static_cast<float>(lay.viewportW()), vh = static_cast<float>(lay.viewportH());
  const double localX = mouseX - lay.viewportX();
  const double localY = mouseY - lay.viewportY();
  const Vec3 eye{
      camTarget_.x + camDist_ * std::cos(camPitch_) * std::sin(camYaw_),
      camTarget_.y + camDist_ * std::sin(camPitch_),
      camTarget_.z + camDist_ * std::cos(camPitch_) * std::cos(camYaw_)};
  const Mat4 view = Mat4::lookAt(eye, camTarget_, {0, 1, 0});
  const float aspect = vw / (vh > 0 ? vh : 1);

  // Ponto no near plane: NDC -> mundo.
  const float ndcX = static_cast<float>(localX) / vw * 2.f - 1.f;
  const float ndcY = 1.f - static_cast<float>(localY) / vh * 2.f;
  const float tanHalf = std::tan(radians(35.f));
  Vec3 forward = normalize(sub(camTarget_, eye));
  Vec3 right = normalize(cross(forward, {0, 1, 0}));
  Vec3 up = cross(right, forward);
  Vec3 dir = normalize(forward + mul(right, ndcX * tanHalf * aspect) + mul(up, ndcY * tanHalf));

  // Blocos primeiro (empate na superfície favorece o bloco — era a causa da
  // baseplate "não selecionável" por cima: o plano y=0 empatava com o topo
  // dela e o chão ganhava).
  Vec3 best = eye + mul(dir, 400.f);
  float bestT = 400.f;
  int bestIndex = -1;
  for (size_t index = 0; index < world_.blocks.size(); ++index) {
    // Objetos travados ou ocultos não são selecionáveis pelo viewport.
    if (index < blockLocked_.size() && blockLocked_[index]) continue;
    if (index < blockHidden_.size() && blockHidden_[index]) continue;
    const Block& block = world_.blocks[index];
    Vec3 bmin, bmax;
    World::bounds(block, bmin, bmax);
    float t = 0;
    Vec3 normal;
    if (rayAabb(eye, dir, bmin, bmax, t, normal) && t < bestT) {
      bestT = t;
      best = eye + mul(dir, t);
      bestIndex = static_cast<int>(index);
      result.normal = normal;
    }
  }

  // Chão y=0 por último — só vence se estiver ESTRITAMENTE mais perto.
  float tPlane = 0;
  if (rayPlaneY(eye, dir, 0.f, tPlane) && tPlane < bestT - 0.001f) {
    bestT = tPlane;
    best = eye + mul(dir, tPlane);
    bestIndex = -1;
  }
  result.hit = true;
  result.point = best;
  result.distance = bestT;
  result.blockIndex = bestIndex;
  return result;
}

EditorResult EditorApp::run() {
  std::string error;
  if (!initWindow(error)) {
    std::fprintf(stderr, "[loomia-studio] %s\n", error.c_str());
    return EditorResult::Quit;
  }

  glfwGetFramebufferSize(window_, &fbw_, &fbh_);
  ui_.width = fbw_;
  ui_.height = fbh_;

  double lastTime = glfwGetTime();
  bool running = true;

  // Abrir por linha de comando: LOOMIA_STUDIO_OPEN=arquivo.loom
  if (const char* openArg = std::getenv("LOOMIA_STUDIO_OPEN")) {
    std::string err;
    if (!openLoom(openArg, err)) setStatus(std::string("Erro ao abrir ") + openArg + ": " + err);
    unsetenv("LOOMIA_STUDIO_OPEN");
  }

  while (running) {
    if (exitRequested_) break;
    if (glfwWindowShouldClose(window_)) {
      if (inEditor_ && dirty_) {
        if (!confirmExit_) {
          // Alterações não salvas: pergunta antes de sair.
          confirmExit_ = true;
          glfwSetWindowShouldClose(window_, GLFW_FALSE);
        }
        // Modal aberto: continua o loop até o usuário responder;
        // o modal seta exitRequested_ (fechar) ou limpa dirty_ (continuar).
      } else {
        break;  // sem alterações: sai direto
      }
    }
    const double now = glfwGetTime();
    const float dt = static_cast<float>(now - lastTime);
    lastTime = now;

    glfwPollEvents();
    clickConsumed_ = false;
    if (uiCooldown_ > 0) --uiCooldown_;
    tickAutosave(dt);

    // ---------- câmera: botão direito orbita, roda dá zoom, WASD move o alvo
    // (SÓ dentro do viewport central; com modal aberto nenhuma câmera mexe)
    double probeX = 0, probeY = 0;
    glfwGetCursorPos(window_, &probeX, &probeY);
    const bool cursorInViewport = [this](double x, double y) {
      const LayoutRects lay = layout();
      return x >= lay.viewportX() && x <= lay.viewportX() + lay.viewportW() &&
             y >= lay.viewportY() && y <= lay.viewportY() + lay.viewportH();
    }(probeX, probeY);
    const bool cameraAllowed = inEditor_ && !confirmExit_ && cursorInViewport && uiCooldown_ == 0;

    if (cameraAllowed && glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
      double x, y;
      glfwGetCursorPos(window_, &x, &y);
      if (!orbiting_) { orbiting_ = true; lastX_ = x; lastY_ = y; }
      const float invert = prefsInvertOrbit_ ? -1.f : 1.f;
      camYaw_ -= static_cast<float>(x - lastX_) * 0.008f * prefsOrbitSpeed_ * invert;
      camPitch_ = std::clamp(camPitch_ + static_cast<float>(y - lastY_) * 0.006f * prefsOrbitSpeed_ * invert, -1.4f, 1.5f);
      lastX_ = x;
      lastY_ = y;
    } else {
      orbiting_ = false;
    }

    // Botão do meio: pan do mapa (só dentro do viewport e sem modal).
    if (cameraAllowed && glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
      double x, y;
      glfwGetCursorPos(window_, &x, &y);
      if (!panning_) { panning_ = true; lastX_ = x; lastY_ = y; }
      const float scale = camDist_ * 0.0016f;
      // Pan no plano do chão (X/Z): mouse puxa o mundo na direção da tela.
      // right = (cosY, 0, -sinY); forward no plano = (-sinY, 0, -cosY).
      const float cosY = std::cos(camYaw_), sinY = std::sin(camYaw_);
      const float dx = static_cast<float>(x - lastX_);
      const float dy = static_cast<float>(y - lastY_);
      camTarget_.x += (-cosY * dx - sinY * dy) * scale;
      camTarget_.z += (sinY * dx - cosY * dy) * scale;
      lastX_ = x;
      lastY_ = y;
    } else {
      panning_ = false;
    }

    updateCamera(dt);

    // ---------- interação com a viewport (botão esquerdo)
    // Modal aberto captura tudo: nenhum clique chega à viewport/3D.
    // Modo teste (mini-client): Esc sai; o update roda por frame.
    if (inEditor_ && playTestOpen_) {
      if (keys_[256] /*ESC*/) { keys_[256] = false; endTestPlay(); }
      updateTestPlay(dt);
    }

    // Modal de preferências: viewport inteiramente bloqueado enquanto aberto.
    if (inEditor_ && prefsOpen_) {
      // (nenhuma interação de edição — o popup atende no final do frame)
    }

    if (inEditor_ && !confirmExit_ && !playTestOpen_ && !prefsOpen_) {
      double mx, my;
      glfwGetCursorPos(window_, &mx, &my);
      const LayoutRects layNow = layout();
      const bool inViewport = my > layNow.viewportY() && my < layNow.viewportY() + layNow.viewportH() &&
                              mx > layNow.viewportX() && mx < layNow.viewportX() + layNow.viewportW();
      const bool leftDown = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
      if (inViewport && leftDown && !dragging_ && !mouseWasDown_ && !clickConsumed_) {
        clickConsumed_ = true;
        dragging_ = true;
        // Gizmo primeiro: se o clique pegou um minicubo, ele consome o clique
        // (o switch não roda e não sobrescreve a seleção).
        bool gizmoHandled = (tool_ == 2 || tool_ == 3) && updateGizmoTryPick(mx, my);
        const RayHit hit = pickBlock(mx, my, fbw_, fbh_);
        if (hit.hit && !gizmoHandled) {
          switch (tool_) {
            case 0: {  // Add: coloca bloco adjacente à face atingida
              pushUndo();
              Block block;
              block.size = {currentSize_[0], currentSize_[1], currentSize_[2]};
              block.color = {static_cast<float>(currentColor_[0]), static_cast<float>(currentColor_[1]), static_cast<float>(currentColor_[2])};
              block.material = static_cast<Material>(currentMaterial_);
              Vec3 pos = hit.point;
              if (hit.blockIndex >= 0) pos = pos + mul(hit.normal, 0.01f);
              if (snapEnabled_) { pos.x = std::round(pos.x); pos.y = std::round(pos.y); pos.z = std::round(pos.z); }
              // Centraliza o bloco novo na célula
              pos.y += block.size.y * 0.5f;
              block.position = pos;
              world_.blocks.push_back(block);
              selected_ = static_cast<int>(world_.blocks.size()) - 1;
              renderer_.upload(world_);
              markDirty();
              break;
            }
            case 1: {  // Select: 1 bloco no clique; retângulo se ARRASTAR
              selected_ = hit.blockIndex;
              selection_.clear();
              if (selected_ >= 0) {
                selection_.push_back(selected_);
                setStatus("Bloco selecionado. Arraste para selecionar vários (retângulo).");
              }
              break;
            }
            case 2:   // Move: seleciona o bloco clicado como âncora dos gizmos
            case 3:   // Scale: idem
              selected_ = hit.blockIndex;
              if (selected_ >= 0) {
                selection_.clear();
                selection_.push_back(selected_);
                setStatus("Bloco escolhido — arraste os minicubos das faces.");
              }
              break;
            case 4:  // Paint
              if (hit.blockIndex >= 0) {
                pushUndo();
                world_.blocks[static_cast<size_t>(hit.blockIndex)].color = {
                    static_cast<float>(currentColor_[0]), static_cast<float>(currentColor_[1]), static_cast<float>(currentColor_[2])};
                world_.blocks[static_cast<size_t>(hit.blockIndex)].material = static_cast<Material>(currentMaterial_);
                renderer_.upload(world_);
                markDirty();
              }
              break;
            case 5:  // Erase
              if (hit.blockIndex >= 0) {
                pushUndo();
                world_.blocks.erase(world_.blocks.begin() + hit.blockIndex);
                if (selected_ == hit.blockIndex) selected_ = -1;
                renderer_.upload(world_);
                markDirty();
              }
              break;
            default:
              break;
          }
        }
      }
      if (!leftDown) {
        dragging_ = false;
        if (gizmoDragging_) {
          gizmoDragging_ = false;
          renderer_.upload(world_);
        }
      }

      // Arrasto do gizmo (o clique foi consumido pelo TryPick acima).
      if (tool_ == 2 || tool_ == 3) updateGizmoDrag(mx, my, leftDown);

      // Retângulo de seleção (ferramenta Select + arrastar): acompanha o mouse.
      if (tool_ == 1) {
        if (dragging_ && leftDown) {
          if (!marqueeActive_) { marqueeActive_ = true; marqueeStart_[0] = static_cast<float>(mx); marqueeStart_[1] = static_cast<float>(my); }
          marqueeNow_[0] = static_cast<float>(mx);
          marqueeNow_[1] = static_cast<float>(my);
        } else if (marqueeActive_ && !leftDown) {
          // Solta o botão: seleciona todos os blocos cujo centro projetado
          // cai dentro do retângulo (projeção via pickBlock por amostragem).
          const float x0 = std::min(marqueeStart_[0], marqueeNow_[0]);
          const float x1 = std::max(marqueeStart_[0], marqueeNow_[0]);
          const float y0 = std::min(marqueeStart_[1], marqueeNow_[1]);
          const float y1 = std::max(marqueeStart_[1], marqueeNow_[1]);
          selection_.clear();
          for (size_t index = 0; index < world_.blocks.size(); ++index) {
            // Projeta o centro do bloco: cast um raio para ele e mede a tela.
            const Block& block = world_.blocks[index];
            const Vec3 eye{
                camTarget_.x + camDist_ * std::cos(camPitch_) * std::sin(camYaw_),
                camTarget_.y + camDist_ * std::sin(camPitch_),
                camTarget_.z + camDist_ * std::cos(camPitch_) * std::cos(camYaw_)};
            // Projeção no espaço do VIEWPORT (não da janela inteira).
            const LayoutRects marqueeLay = layout();
            const float mvx = static_cast<float>(marqueeLay.viewportX());
            const float mvy = static_cast<float>(marqueeLay.viewportY());
            const float mvW = static_cast<float>(marqueeLay.viewportW());
            const float mvH = static_cast<float>(marqueeLay.viewportH());
            const float aspect = mvW / (mvH > 0 ? mvH : 1);
            const float tanHalf = std::tan(radians(35.f));
            Vec3 forward = normalize(sub(camTarget_, eye));
            Vec3 right = normalize(cross(forward, {0, 1, 0}));
            Vec3 up = cross(right, forward);
            Vec3 toBlock = block.position - eye;
            const float depth = dot(toBlock, forward);
            if (depth <= 0) continue;
            const Vec3 projected = forward * depth;
            const Vec3 flat = toBlock - projected;
            const float sx = mvx + mvW / 2.f + (dot(flat, right) / (depth * tanHalf * aspect)) * (mvW / 2.f);
            const float sy = mvy + mvH / 2.f - (dot(flat, up) / (depth * tanHalf)) * (mvH / 2.f);
            if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1) selection_.push_back(static_cast<int>(index));
          }
          marqueeActive_ = false;
          if (!selection_.empty()) {
            selected_ = selection_.front();  // âncora: primeiro do retângulo
            setStatus(std::to_string(selection_.size()) + " bloco(s) selecionado(s) — troque para Move/Scale para arrastar.");
          } else {
            selected_ = -1;
          }
        }
      }
    }

    // ---------- render 3D
    glfwMakeContextCurrent(window_);
    glViewport(0, 0, fbw_, fbh_);
    glClearColor(world_.sky.rf(), world_.sky.gf(), world_.sky.bf(), 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (inEditor_ && activeTab_ == 1) {
      // Aba Scripts: SEM cena 3D — o editor de código ocupa o viewport.
      const Theme themeNow = theme();
      glDisable(GL_SCISSOR_TEST);
      glViewport(0, 0, fbw_, fbh_);
      glClearColor(themeNow.bgR, themeNow.bgG, themeNow.bgB, 1.f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    } else if (inEditor_) {
      const LayoutRects lay = layout();
      int vx = lay.viewportX(), vy = lay.viewportY();
      int vw = lay.viewportW(), vh = lay.viewportH();
      // Pré-visualização de proporção (preferência do editor): encaixa a cena
      // numa moldura com a proporção escolhida (livre, 16:9, 4:3, 1:1).
      if (previewAspect_ > 0) {
        const float ratios[] = {0.f, 16.f / 9.f, 4.f / 3.f, 1.f};
        const float target = ratios[previewAspect_];
        float w2 = static_cast<float>(vw), h2 = w2 / target;
        if (h2 > vh) { h2 = static_cast<float>(vh); w2 = h2 * target; }
        vx += (vw - static_cast<int>(w2)) / 2;
        vy += (vh - static_cast<int>(h2)) / 2;
        vw = static_cast<int>(w2);
        vh = static_cast<int>(h2);
      }
      // 1) limpa TODO o framebuffer com a cor de fundo do TEMA (os painéis
      //    da UI usam essa base); o céu do mapa não aparece por fora.
      const Theme themeNow = theme();
      glDisable(GL_SCISSOR_TEST);
      glViewport(0, 0, fbw_, fbh_);
      glClearColor(themeNow.bgR, themeNow.bgG, themeNow.bgB, 1.f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      // 2) limpa SÓ o viewport com a cor do CÉU do mapa e desenha a cena lá.
      glEnable(GL_SCISSOR_TEST);
      glScissor(vx, fbh_ - vy - vh, vw, vh);
      glViewport(vx, fbh_ - vy - vh, vw, vh);
      glClearColor(world_.sky.r / 255.f, world_.sky.g / 255.f, world_.sky.b / 255.f, 1.f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      const Vec3 eye{
          camTarget_.x + camDist_ * std::cos(camPitch_) * std::sin(camYaw_),
          camTarget_.y + camDist_ * std::sin(camPitch_),
          camTarget_.z + camDist_ * std::cos(camPitch_) * std::cos(camYaw_)};
      Camera camera;
      camera.position = eye;
      camera.target = camTarget_;
      const float aspect = static_cast<float>(vw) / static_cast<float>(vh > 0 ? vh : 1);
      // Desenha o mundo SEM os objetos ocultos (cópia filtrada barata: só
      // quando houver algum oculto).
      bool anyHidden = false;
      for (size_t index = 0; index < blockHidden_.size() && index < world_.blocks.size(); ++index) {
        if (blockHidden_[index]) { anyHidden = true; break; }
      }
      if (anyHidden) {
        World visible = world_;
        visible.blocks.clear();
        for (size_t index = 0; index < world_.blocks.size(); ++index) {
          if (index < blockHidden_.size() && blockHidden_[index]) continue;
          visible.blocks.push_back(world_.blocks[index]);
        }
        renderer_.upload(visible);
        renderer_.draw(camera, aspect);
        renderer_.upload(world_);  // restaura a malha completa para picking/etc.
      } else {
        renderer_.draw(camera, aspect);
      }

      // Seleção: todos os blocos destacados com caixa maior semitransparente.
      for (const int index : selection_) {
        if (index < 0 || index >= static_cast<int>(world_.blocks.size())) continue;
        const Block& block = world_.blocks[static_cast<size_t>(index)];
        Mat4 transform = Mat4::translation(block.position);
        transform = transform * Mat4::scale(block.size * 1.08f);
        renderer_.drawBox(camera, aspect, transform, index == selected_ ? Color3{255, 255, 255} : Color3{200, 220, 255}, true);
      }
      // Gizmos: 1 minicubo em cada face do bloco (6 alças). Cor = eixo
      // (vermelho=X, verde=Y, azul=Z). Tamanho proporcional à distância da
      // câmera — na tela fica constante, fácil de clicar de longe ou perto.
      if (selected_ >= 0 && prefsShowGizmos_ && (tool_ == 2 || tool_ == 3)) {
        const Block& block = world_.blocks[static_cast<size_t>(selected_)];  // gizmo do bloco âncora
        const float gizmoScale = std::max(0.6f, camDist_ * 0.035f);
        const float gap = gizmoScale * 0.9f;
        const float cubeSize = gizmoScale * 0.55f;
        const Color3 axisColor[3] = {{220, 60, 60}, {70, 200, 80}, {70, 110, 230}};
        for (int axis = 0; axis < 3; ++axis) {
          for (int sign = -1; sign <= 1; sign += 2) {
            Vec3 offset{0, 0, 0};
            if (axis == 0) offset.x = sign * (block.size.x / 2.f + gap + cubeSize / 2.f);
            if (axis == 1) offset.y = sign * (block.size.y / 2.f + gap + cubeSize / 2.f);
            if (axis == 2) offset.z = sign * (block.size.z / 2.f + gap + cubeSize / 2.f);
            Mat4 transform = Mat4::translation(block.position + offset);
            transform = transform * Mat4::scale({cubeSize, cubeSize, cubeSize});
            const bool active = gizmoDragging_ && gizmoAxis_ == axis;
            renderer_.drawBox(camera, aspect, transform, active ? Color3{255, 230, 60} : axisColor[axis], true);
          }
        }
      }
      glDisable(GL_SCISSOR_TEST);
      glViewport(0, 0, fbw_, fbh_);
    }

    // ---------- UI — shell novo (tema + layout responsivo)
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // O overlay 2D usa o tamanho ATUAL do framebuffer (antes ficava preso ao
    // tamanho do boot — com resize, os rects saíam fora da tela e só o texto
    // aparecia, porque a fonte lê o viewport diretamente).
    ui_.width = fbw_;
    ui_.height = fbh_;
    th_ = theme();
    const Theme& currentTheme = th_;
    if (inEditor_) {
      // Painéis do shell (podem usar viewport/scissor — desenha antes da cena?
      // Não: a cena já foi desenhada; os painéis cobrem as margens).
      drawTopbar(fbw_, fbh_, currentTheme);
      drawTabs(fbw_, fbh_, currentTheme);
      drawToolRail(fbw_, fbh_, currentTheme);
      drawHierarchy(fbw_, fbh_, currentTheme);
      drawProperties(fbw_, fbh_, currentTheme);
      drawAssetBrowser(fbw_, fbh_, currentTheme);
      drawViewportFrame(fbw_, fbh_, currentTheme);
      drawStatusLine(fbw_, fbh_, currentTheme);
      ui_.rect(200, 200, 60, 60, 0.f, 1.f, 0.f, 1.f);  // DEBUG loop
      if (activeTab_ == 1) {
        drawLuaEditor(fbw_, fbh_, currentTheme);
        drawLuaCodeEditor(fbw_, fbh_, currentTheme);
      }
      ui_.flush();
      flushText();
      // Modal de alterações não salvas por cima de tudo.
      drawConfirmExit(fbw_, fbh_);
      ui_.flush();
      flushText();
      if (prefsOpen_) {
        drawPrefs(fbw_, fbh_);
        ui_.flush();
        flushText();
      }
    } else {
      // Welcome: 1) papel dos cards (2D) 2) prévias 3D por cima 3) texto.
      drawWelcome(fbw_, fbh_);
      ui_.flush();
      drawTemplatePreviews(fbw_, fbh_);
      drawWelcomeText(fbw_, fbh_);
      ui_.flush();
      flushText();
    }
    // Retângulo de seleção (marquee) por cima da viewport.
    if (marqueeActive_) {
      const float x0 = std::min(marqueeStart_[0], marqueeNow_[0]);
      const float y0 = std::min(marqueeStart_[1], marqueeNow_[1]);
      const float wRect = std::abs(marqueeNow_[0] - marqueeStart_[0]);
      const float hRect = std::abs(marqueeNow_[1] - marqueeStart_[1]);
      ui_.rect(x0, y0, wRect, hRect, 0.2f, 0.5f, 0.95f, 0.12f);
      ui_.frame(x0, y0, wRect, hRect, 2, 0.2f, 0.5f, 0.95f, 0.9f);
    }
    ui_.flush();
    flushText();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    mouseWasDown_ = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    glfwSwapBuffers(window_);
  }

  ui_.destroy();
  font_.destroy();
  icons_.destroy();
  renderer_.destroy();
  previewRenderer_.destroy();
  previewReady_ = false;

  return EditorResult::Quit;
}

// ------------------------------------------------------------------ welcome

}  // namespace loomia
