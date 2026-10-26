// Loomia Studio: editor visual de mapas .loom (peça principal).
//
// Reusa a engine do cliente: WorldRenderer (OpenGL 3.3), Vec3/Mat4 e o
// formato .loom (world.h). A UI é desenhada com overlay 2D próprio — sem
// toolkit externo, igual ao cliente.
#include "studio.h"
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

constexpr int WINDOW_W = 1440;
constexpr int WINDOW_H = 860;
constexpr int UI_BAR_H = 64;        // barra de ferramentas inferior
constexpr int UI_STATUS_H = 26;     // linha de status
constexpr float GRID_EXTENT = 60.f; // grade de referência

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
struct UiOverlay {
  unsigned int vao = 0, vbo = 0;
  ShaderProgram shader;
  std::vector<float> verts;  // x,y,r,g,b,a (6 floats por vértice, 2 tri/vértice... na verdade triângulos)
  int width = WINDOW_W, height = WINDOW_H;
  bool ready = false;

  bool init(std::string& error) {
    if (!shader.compile(UI_VS, UI_FS, error)) return false;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    ready = true;
    return true;
  }

  void destroy() {
    if (vbo) glDeleteBuffers(1, &vbo);
    if (vao) glDeleteVertexArrays(1, &vao);
    shader.destroy();
    ready = false;
  }

  void tri(float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a) {
    verts.insert(verts.end(), {x0, y0, r, g, b, a, x1, y1, r, g, b, a, x2, y2, r, g, b, a});
  }

  void rect(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f) {
    tri(x, y, x + w, y, x + w, y + h, r, g, b, a);
    tri(x, y, x + w, y + h, x, y + h, r, g, b, a);
  }

  void frame(float x, float y, float w, float h, float t, float r, float g, float b, float a = 1.0f) {
    rect(x, y, w, t, r, g, b, a);
    rect(x, y + h - t, w, t, r, g, b, a);
    rect(x, y, t, h, r, g, b, a);
    rect(x + w - t, y, t, h, r, g, b, a);
  }

  // Texto em escala inteira (cel_size px por célula do glifo 5x7).
  void text(const std::string& str, float x, float y, float cell, float r, float g, float b, float a = 1.0f) {
    float cx = x;
    for (char ch : str) {
      const unsigned char c = static_cast<unsigned char>(ch);
      if (ch == '\n') { y += cell * 8; cx = x; continue; }
      if (c < 32 || c > 126) { cx += cell * 6; continue; }
      const Glyph5x7& glyph = kFont[c - 32];
      // Defesa: glifo ausente na tabela nunca derruba o studio.
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

  float textWidth(const std::string& str, float cell) {
    return static_cast<float>(str.size()) * cell * 6;
  }

  void flush() {
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
};

// ---------------------------------------------------------------- utilidades

struct RayHit {
  bool hit = false;
  Vec3 point;
  float distance = 0;
  int blockIndex = -1;  // -1 = chão/grade
  Vec3 normal;          // normal da face atingida
};

Vec3 sub(const Vec3& a, const Vec3& b) { return a - b; }
Vec3 mul(const Vec3& a, float s) { return a * s; }

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

std::string lowercaseCopy(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

}  // namespace

// ============================================================================
// EditorApp
// ============================================================================

class EditorApp {
 public:
  EditorResult run();
  ~EditorApp();

 private:
  // ---- janela/engine
  bool initWindow(std::string& error);
  void shutdown();
  void newFromTemplate(int templateIndex);
  bool openLoom(const std::string& path, std::string& error);
  bool saveLoom(const std::string& path, std::string& error);
  void markDirty();
  void pushUndo();
  void undo();
  void redo();

  // ---- interação
  void updateCamera(float dt);
  RayHit pickBlock(double mouseX, double mouseY, int fbw, int fbh);
  void deleteSelection();
  void duplicateSelection();
  void focusSelection();
  void snapSelection();

  // ---- UI
  void drawUi(int fbw, int fbh);
  void drawWelcome(int fbw, int fbh);
  int buttonHit(double mx, double my, float x, float y, float w, float h) const;
  void setStatus(const std::string& message);
  // Texto vai para uma fila: os rects são desenhados PRIMEIRO (ui_.flush) e
  // o texto DEPOIS — evita que o estado GL da fonte quebre os rects em
  // drivers rigorosos (bug da interface invisível).
  struct TextDraw { std::string str; float x, y, size, r, g, b, a; };
  void uiText(const std::string& str, float x, float y, float px, float r, float g, float b, float a = 1.0f);
  void flushText();
  std::vector<TextDraw> textQueue_;

  GLFWwindow* window_ = nullptr;
  WorldRenderer renderer_;
  UiOverlay ui_;
  UiFont font_;
  std::string fontError_;

  World world_;
  std::string filePath_;         // vazio = nunca salvo
  bool dirty_ = false;
  bool inEditor_ = false;        // false = welcome screen
  std::string status_ = "Bem-vindo ao Loomia Studio!";
  std::string modalMessage_;

  // câmera orbital
  float camYaw_ = 0.8f;
  float camPitch_ = 0.5f;
  float camDist_ = 34.f;
  Vec3 camTarget_{0, 2, 0};

  // ferramenta
  int tool_ = 0;  // 0=add, 1=select, 2=move, 3=scale, 4=paint, 5=erase
  int selected_ = -1;
  int hover_ = -1;
  int currentMaterial_ = 4;  // plastic
  int currentColor_[3] = {230, 120, 60};
  float currentSize_[3] = {1, 1, 1};
  bool snapEnabled_ = true;

  // input
  bool dragging_ = false;
  bool orbiting_ = false;
  bool panning_ = false;
  double lastX_ = 0, lastY_ = 0;
  bool clickConsumed_ = false;   // um clique só age em UM widget por frame
  bool mouseWasDown_ = false;    // estado do botão esquerdo no frame anterior

  // Gizmos: 0=nenhum, 1=eixos X/Y/Z de movimento, 2=alças de escala.
  int gizmoMode_ = 0;
  int gizmoAxis_ = -1;       // 0=x, 1=y, 2=z (-1 = nenhum pego)
  Vec3 gizmoBlockStart_{};
  Vec3 gizmoBlockStartSize_{};
  float gizmoStartParam_ = 0;    // parâmetro escalar do bloco no clique
  float gizmoMouseParam_ = 0;    // parâmetro escalar do mouse no clique
  float gizmoFaceSign_ = 1;      // face arrastada: +1 (positiva) ou -1 (negativa)
  std::vector<Vec3> selStartPos_;     // posições iniciais da seleção no arrasto
  std::vector<Vec3> selStartSize_;    // tamanhos iniciais da seleção no arrasto
  float gizmoRayDist_ = 0;       // distância olho→bloco no clique (arrasto estável)
  float gizmoStartDot_ = 0;      // dot((ray(t)-ray0(t)), eixo) no clique

  // Menu "Arquivo" (dropdown da toolbar) e arquivos recentes.
  bool fileMenu_ = false;
  bool colorPicker_ = false;     // dropdown de 256 cores (HSL grid)

  // Seleção múltipla (ferramenta Select desenha retângulo; Move/Scale editam
  // o conjunto — cada bloco com seus próprios gizmos de face).
  std::vector<int> selection_;
  bool marqueeActive_ = false;
  float marqueeStart_[2] = {0, 0};
  float marqueeNow_[2] = {0, 0};

  float pickerHue_ = 0, pickerSat_ = 0.75f, pickerVal_ = 0.85f;
  std::vector<std::string> recentList_;
  void loadRecents();
  void saveRecents();
  void addRecent(const std::string& path);
  bool saveWithDialog();          // pede caminho e salva; false se cancelado
  void renameMap();               // renomeia o mapa (nome exibido/id)
  void saveCopy();                // cria cópia em novo arquivo
  void backToWelcome();           // volta para a tela inicial (com confirm)

  // Modal de saída (alterações não salvas).
  bool confirmExit_ = false;      // "salvar antes de sair?"
  bool exitRequested_ = false;    // usuário confirmou "Não salvar" no fechamento
  void drawConfirmExit(int fbw, int fbh);
  bool gizmoDragging_ = false;

  // Menu de cena aberto (cor do céu via HSL simples).
  bool sceneMenu_ = false;
  float skyHue_ = 0.58f, skySat_ = 0.5f, skyVal_ = 0.95f;

  // Helpers de gizmo/cena.
  void updateGizmo(double mx, double my, bool clicked, bool held);
  void drawSelectionOverlay(float fbw, float fbh);
  void drawSceneMenu(float fbw, float fbh);
  void hslToRgb(float h, float s, float v, float out[3]) const;

  // Prévia dos templates: cada card renderiza o template real num viewport.
  void drawTemplatePreviews(int fbw, int fbh);
  WorldRenderer previewRenderer_;
  bool previewReady_ = false;
  bool keys_[512] = {};
  int fbw_ = WINDOW_W, fbh_ = WINDOW_H;

  // undo/redo: cópias do mundo
  std::vector<World> undoStack_;
  std::vector<World> redoStack_;
};

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
      self->camDist_ = std::clamp(self->camDist_ - static_cast<float>(yoff) * 2.5f, 4.f, 220.f);
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
    fontCandidates.push_back(std::string(root) + "/misc/fonts/Comic_Neue/ComicNeue-Regular.ttf");
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
        fontCandidates.push_back(base + "/misc/fonts/Comic_Neue/ComicNeue-Regular.ttf");
        fontCandidates.push_back(base + "/../misc/fonts/Comic_Neue/ComicNeue-Regular.ttf");
      }
    }
  }
  fontCandidates.push_back("/media/adryan/imaginedevel/Projetos/loomia/misc/fonts/Comic_Neue/ComicNeue-Regular.ttf");
  for (const std::string& candidate : fontCandidates) {
    std::string fontError;
    if (font_.init(candidate, fontError)) break;
    fontError_ = fontError;
  }
  // Renderer das prévias dos templates (contexto já é o corrente).
  loadRecents();
  std::string previewError;
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
  if (selected_ >= static_cast<int>(world_.blocks.size())) selected_ = -1;
  renderer_.upload(world_);
  dirty_ = true;
  setStatus("Desfeito.");
}

void EditorApp::redo() {
  if (redoStack_.empty()) { setStatus("Nada para refazer."); return; }
  undoStack_.push_back(world_);
  world_ = redoStack_.back();
  redoStack_.pop_back();
  renderer_.upload(world_);
  dirty_ = true;
  setStatus("Refeito.");
}

// ------------------------------------------------------------------ arquivo

void EditorApp::newFromTemplate(int templateIndex) {
  const TemplateId ids[] = {TemplateId::Baseplate, TemplateId::SkyIslands, TemplateId::Obby, TemplateId::Showcase};
  world_ = makeTemplate(ids[std::clamp(templateIndex, 0, 3)]);
  filePath_.clear();
  dirty_ = false;
  selected_ = -1;
  selection_.clear();
  undoStack_.clear();
  redoStack_.clear();
  renderer_.upload(world_);
  inEditor_ = true;
  setStatus("Novo mapa criado. Ctrl+S salva, Ctrl+Shift+S salva como...");
}

bool EditorApp::openLoom(const std::string& path, std::string& error) {
  if (!loadWorld(path, world_, error)) return false;
  filePath_ = path;
  dirty_ = false;
  selected_ = -1;
  selection_.clear();
  undoStack_.clear();
  redoStack_.clear();
  renderer_.upload(world_);
  inEditor_ = true;
  setStatus("Mapa carregado: " + path);
  return true;
}

bool EditorApp::saveLoom(const std::string& path, std::string& error) {
  if (!saveWorld(path, world_, error)) return false;
  filePath_ = path;
  dirty_ = false;
  setStatus("Salvo em " + path);
  return true;
}

// ------------------------------------------------------------------ câmera

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
  // Constrói o raio da câmera para o pixel.
  const Vec3 eye{
      camTarget_.x + camDist_ * std::cos(camPitch_) * std::sin(camYaw_),
      camTarget_.y + camDist_ * std::sin(camPitch_),
      camTarget_.z + camDist_ * std::cos(camPitch_) * std::cos(camYaw_)};
  const Mat4 view = Mat4::lookAt(eye, camTarget_, {0, 1, 0});
  const float aspect = static_cast<float>(fbw) / static_cast<float>(fbh > 0 ? fbh : 1);
  const Mat4 proj = Mat4::perspective(radians(70.f), aspect, 0.1f, 1500.f);

  // Ponto no near plane: NDC -> mundo.
  const float ndcX = static_cast<float>(mouseX) / fbw * 2.f - 1.f;
  const float ndcY = 1.f - static_cast<float>(mouseY) / fbh * 2.f;
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

void EditorApp::deleteSelection() {
  if (selection_.empty()) {
    if (selected_ < 0 || selected_ >= static_cast<int>(world_.blocks.size())) return;
    selection_.push_back(selected_);
  }
  pushUndo();
  // Remove do maior índice para o menor (índices estáveis).
  std::sort(selection_.rbegin(), selection_.rend());
  for (const int index : selection_) {
    if (index >= 0 && index < static_cast<int>(world_.blocks.size())) {
      world_.blocks.erase(world_.blocks.begin() + index);
    }
  }
  setStatus(std::to_string(selection_.size()) + " bloco(s) removido(s) (Delete).");
  selection_.clear();
  selected_ = -1;
  renderer_.upload(world_);
  markDirty();
}

void EditorApp::duplicateSelection() {
  if (selection_.empty()) {
    if (selected_ < 0 || selected_ >= static_cast<int>(world_.blocks.size())) return;
    selection_.push_back(selected_);
  }
  pushUndo();
  std::vector<int> originals = selection_;  // índices crescentes
  for (const int index : originals) {
    if (index < 0 || index >= static_cast<int>(world_.blocks.size())) continue;
    Block copy = world_.blocks[static_cast<size_t>(index)];
    copy.position.z += copy.size.z;  // encosta na frente do original
    world_.blocks.push_back(copy);
  }
  // A seleção passa a apontar para as cópias (no fim do vetor).
  const int total = static_cast<int>(world_.blocks.size());
  selection_.clear();
  for (size_t step = 0; step < originals.size(); ++step) {
    selection_.push_back(total - static_cast<int>(originals.size()) + static_cast<int>(step));
  }
  selected_ = selection_.back();
  renderer_.upload(world_);
  markDirty();
  setStatus(std::to_string(originals.size()) + " bloco(s) duplicado(s) (Ctrl+D).");
}

void EditorApp::focusSelection() {
  if (selected_ < 0 || selected_ >= static_cast<int>(world_.blocks.size())) return;
  camTarget_ = world_.blocks[static_cast<size_t>(selected_)].position;
  setStatus("Câmera centralizada no bloco (F).");
}

void EditorApp::snapSelection() {
  std::vector<int> targets = selection_;
  if (targets.empty() && selected_ >= 0) targets.push_back(selected_);
  if (targets.empty()) return;
  pushUndo();
  for (const int index : targets) {
    if (index < 0 || index >= static_cast<int>(world_.blocks.size())) continue;
    Block& block = world_.blocks[static_cast<size_t>(index)];
    block.position.x = std::round(block.position.x);
    block.position.y = std::round(block.position.y);
    block.position.z = std::round(block.position.z);
  }
  renderer_.upload(world_);
  markDirty();
  setStatus("Posições arredondadas (G).");
}

int EditorApp::buttonHit(double mx, double my, float x, float y, float w, float h) const {
  return (mx >= x && mx <= x + w && my >= y && my <= y + h) ? 1 : 0;
}

// ------------------------------------------------------------------ loop

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
    if (glfwWindowShouldClose(window_)) {
      if (inEditor_ && dirty_ && !confirmExit_) {
        // Alterações não salvas: pergunta antes de sair.
        confirmExit_ = true;
        glfwSetWindowShouldClose(window_, GLFW_FALSE);
      } else if (!inEditor_ || !dirty_) {
        break;  // sem alterações (ou na welcome): sai direto
      }
      // confirmExit_ ativo: o loop continua até o usuário responder.
    }
    if (exitRequested_) break;
    const double now = glfwGetTime();
    const float dt = static_cast<float>(now - lastTime);
    lastTime = now;

    glfwPollEvents();
    clickConsumed_ = false;

    // ---------- câmera: botão direito orbita, roda dá zoom, WASD move o alvo
    if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
      double x, y;
      glfwGetCursorPos(window_, &x, &y);
      if (!orbiting_) { orbiting_ = true; lastX_ = x; lastY_ = y; }
      camYaw_ -= static_cast<float>(x - lastX_) * 0.008f;
      camPitch_ = std::clamp(camPitch_ + static_cast<float>(y - lastY_) * 0.006f, -1.4f, 1.5f);
      lastX_ = x;
      lastY_ = y;
    } else {
      orbiting_ = false;
    }

    // Botão do meio: pan do mapa (move o alvo da câmera no plano da tela).
    if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS) {
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
    if (inEditor_) {
      double mx, my;
      glfwGetCursorPos(window_, &mx, &my);
      const bool inViewport = my > UI_BAR_H && my < (fbh_ - UI_STATUS_H);
      const bool leftDown = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
      if (inViewport && leftDown && !dragging_ && !mouseWasDown_ && !clickConsumed_) {
        clickConsumed_ = true;
        dragging_ = true;
        const RayHit hit = pickBlock(mx, my, fbw_, fbh_);
        if (hit.hit) {
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

      // Gizmos de mover/escalar: com ferramenta 2 ou 3, arraste as setas.
      if (tool_ == 2 || tool_ == 3) updateGizmo(mx, my, leftDown, dragging_);

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
            const float aspect = static_cast<float>(fbw_) / static_cast<float>(fbh_ > 0 ? fbh_ : 1);
            const float tanHalf = std::tan(radians(35.f));
            Vec3 forward = normalize(sub(camTarget_, eye));
            Vec3 right = normalize(cross(forward, {0, 1, 0}));
            Vec3 up = cross(right, forward);
            Vec3 toBlock = block.position - eye;
            const float depth = dot(toBlock, forward);
            if (depth <= 0) continue;
            const Vec3 projected = forward * depth;
            const Vec3 flat = toBlock - projected;
            const float sx = fbw_ / 2.f + (dot(flat, right) / (depth * tanHalf * aspect)) * (fbw_ / 2.f);
            const float sy = fbh_ / 2.f - (dot(flat, up) / (depth * tanHalf)) * (fbh_ / 2.f);
            if (sx >= x0 && sx <= x1 && sy >= y0 && sy <= y1) selection_.push_back(static_cast<int>(index));
          }
          marqueeActive_ = false;
          setStatus(std::to_string(selection_.size()) + " bloco(s) selecionado(s).");
        }
      }
    }

    // ---------- render 3D
    glfwMakeContextCurrent(window_);
    glViewport(0, 0, fbw_, fbh_);
    glClearColor(world_.sky.rf(), world_.sky.gf(), world_.sky.bf(), 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (inEditor_) {
      const Vec3 eye{
          camTarget_.x + camDist_ * std::cos(camPitch_) * std::sin(camYaw_),
          camTarget_.y + camDist_ * std::sin(camPitch_),
          camTarget_.z + camDist_ * std::cos(camPitch_) * std::cos(camYaw_)};
      Camera camera;
      camera.position = eye;
      camera.target = camTarget_;
      const float aspect = static_cast<float>(fbw_) / static_cast<float>(fbh_ > 0 ? fbh_ : 1);
      renderer_.draw(camera, aspect);

      // Seleção: todos os blocos destacados com caixa maior semitransparente.
      for (const int index : selection_) {
        if (index < 0 || index >= static_cast<int>(world_.blocks.size())) continue;
        const Block& block = world_.blocks[static_cast<size_t>(index)];
        Mat4 transform = Mat4::translation(block.position);
        transform = transform * Mat4::scale(block.size * 1.08f);
        renderer_.drawBox(camera, aspect, transform, index == selected_ ? Color3{255, 255, 255} : Color3{200, 220, 255}, true);
      }
      // Gizmos: 1 minicubo em cada face do bloco (6 alças). A cor do cubo
      // indica o eixo da face (vermelho=X, verde=Y, azul=Z). Arrastar um
      // minicubo move (ferramenta 2) ou estica/encolhe (ferramenta 3) o bloco
      // naquele eixo, dos dois lados.
      if (selected_ >= 0 && (tool_ == 2 || tool_ == 3)) {
        const Block& block = world_.blocks[static_cast<size_t>(selected_)];  // gizmo do bloco âncora
        const float cubeSize = 0.34f;
        // Faces: ±X (vermelho), ±Y (verde), ±Z (azul).
        const Color3 axisColor[3] = {{220, 60, 60}, {70, 200, 80}, {70, 110, 230}};
        for (int axis = 0; axis < 3; ++axis) {
          for (int sign = -1; sign <= 1; sign += 2) {
            Vec3 offset{0, 0, 0};
            if (axis == 0) offset.x = sign * (block.size.x / 2.f + cubeSize / 2.f);
            if (axis == 1) offset.y = sign * (block.size.y / 2.f + cubeSize / 2.f);
            if (axis == 2) offset.z = sign * (block.size.z / 2.f + cubeSize / 2.f);
            Mat4 transform = Mat4::translation(block.position + offset);
            transform = transform * Mat4::scale({cubeSize, cubeSize, cubeSize});
            const bool active = gizmoDragging_ && gizmoAxis_ == axis;
            renderer_.drawBox(camera, aspect, transform, active ? Color3{255, 230, 60} : axisColor[axis], true);
          }
        }
      }
    }

    // ---------- UI
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (inEditor_) {
      drawUi(fbw_, fbh_);
      drawSceneMenu(fbw_, fbh_);
    } else {
      // Prévias 3D dos templates primeiro (viewport próprio), depois a UI 2D.
      drawTemplatePreviews(fbw_, fbh_);
      drawWelcome(fbw_, fbh_);
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
    // Modal de alterações não salvas por cima de tudo.
    ui_.flush();
    flushText();
    drawConfirmExit(fbw_, fbh_);
    ui_.flush();
    flushText();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    mouseWasDown_ = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    glfwSwapBuffers(window_);
  }

  ui_.destroy();
  font_.destroy();
  renderer_.destroy();
  previewRenderer_.destroy();
  previewReady_ = false;

  return EditorResult::Quit;
}

// ------------------------------------------------------------------ welcome

void EditorApp::drawWelcome(int fbw, int fbh) {
  // Fundo suave
  ui_.rect(0, 0, static_cast<float>(fbw), static_cast<float>(fbh), 0.94f, 0.93f, 0.88f, 1.f);
  uiText("Loomia Studio", fbw / 2.f - font_.textWidth("Loomia Studio", 42) / 2, 100, 42, 0.08f, 0.12f, 0.17f);
  uiText("Crie e edite seus próprios mundos, depois, compartilhe com seus amigos!", fbw / 2.f - font_.textWidth("edite mapas .loom com a mesma engine do cliente", 18) / 2, 160, 18, 0.3f, 0.35f, 0.42f);
  uiText("Crie e edite seus próprios mundos, depois, compartilhe com seus amigos!", fbw / 2.f - font_.textWidth("Atenção, Isso é um trabalho em desenvolvimento, qualquer alteração pode causar problemas entre os mapas, seja cuidadoso!", 18) / 2, 160, 18, 0.3f, 0.35f, 0.42f);

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
    if (!clickConsumed_ && buttonHit(mx, my, x, y, cardW, cardH) &&
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

void EditorApp::drawUi(int fbw, int fbh) {
  const float W = static_cast<float>(fbw), H = static_cast<float>(fbh);
  double mx, my;
  glfwGetCursorPos(window_, &mx, &my);
  const bool down = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  const bool clicked = down && !mouseWasDown_;

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

  // ---------- paleta de cores (16 básicas) + botão "Mais cores" (256, HSL)
  const unsigned char palette[][3] = {
      {230, 120, 60}, {230, 190, 40}, {100, 160, 60}, {70, 150, 200}, {110, 80, 190},
      {230, 230, 235}, {70, 70, 80}, {190, 60, 60},  {240, 160, 200}, {60, 200, 170}};
  for (int index = 0; index < 10; ++index) {
    const float x = 660.f + index * 30.f;   // após o botão Cena (antes: 540, sobrepunha)
    const float y = barY + 12.f;
    const bool match = currentColor_[0] == palette[index][0] && currentColor_[1] == palette[index][1] && currentColor_[2] == palette[index][2];
    ui_.rect(x, y, 24.f, 24.f, palette[index][0] / 255.f, palette[index][1] / 255.f, palette[index][2] / 255.f, 1.f);
    ui_.frame(x, y, 24.f, 24.f, match ? 3.f : 1.f, 0.08f, 0.12f, 0.17f);
    if (clicked && buttonHit(mx, my, x, y, 24.f, 24.f)) {
      currentColor_[0] = palette[index][0]; currentColor_[1] = palette[index][1]; currentColor_[2] = palette[index][2];
    }
  }

  // ---------- botão Cena (cor do céu)
  {
    const float x = 540.f + 10 * 30.f + 12.f, y = barY + 12.f;
    ui_.rect(x, y, 76.f, 40.f, 0.96f, 0.96f, 0.96f);
    ui_.frame(x, y, 76.f, 40.f, 2, 0.08f, 0.12f, 0.17f);
    uiText("Cena (C)", x + 12, y + 14, 13, 0.3f, 0.35f, 0.42f);
    if (clicked && buttonHit(mx, my, x, y, 76.f, 40.f)) sceneMenu_ = !sceneMenu_;
  }

  // ---------- botão "Mais cores" (dropdown 256 cores HSL)
  {
    const float x = 660.f + 10 * 30.f + 12.f, y = barY + 12.f;
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

// Gizmo: clique no minicubo de uma face e arraste. O arrasto usa o ponto do
// raio a uma distância FIXA (a do clique) — o delta projetado no eixo fica
// estável, sem depender do plano que se move junto com o bloco.
void EditorApp::updateGizmo(double mx, double my, bool clicked, bool held) {
  if (selected_ < 0 || selected_ >= static_cast<int>(world_.blocks.size())) return;
  Block& block = world_.blocks[static_cast<size_t>(selected_)];

  const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  // Folga/tamanho IGUAIS ao desenho dos gizmos (ver drawUi do loop 3D).
  const float gap = std::max(0.5f, std::max({block.size.x, block.size.y, block.size.z}) * 0.08f);
  const float cube = std::min(0.45f, gap * 0.5f) * 1.35f;  // hitbox 35% maior

  auto eyePos = [&] {
    return Vec3{
        camTarget_.x + camDist_ * std::cos(camPitch_) * std::sin(camYaw_),
        camTarget_.y + camDist_ * std::sin(camPitch_),
        camTarget_.z + camDist_ * std::cos(camPitch_) * std::cos(camYaw_)};
  };
  auto mouseRay = [&](const Vec3& eye) {
    const float aspect = static_cast<float>(fbw_) / static_cast<float>(fbh_ > 0 ? fbh_ : 1);
    const float ndcX = static_cast<float>(mx) / fbw_ * 2.f - 1.f;
    const float ndcY = 1.f - static_cast<float>(my) / fbh_ * 2.f;
    const float tanHalf = std::tan(radians(35.f));
    Vec3 forward = normalize(sub(camTarget_, eye));
    Vec3 right = normalize(cross(forward, {0, 1, 0}));
    Vec3 up = cross(right, forward);
    return normalize(forward + mul(right, ndcX * tanHalf * aspect) + mul(up, ndcY * tanHalf));
  };

  if (clicked && !gizmoDragging_) {
    const Vec3 eye = eyePos();
    const Vec3 dir = mouseRay(eye);

    // Hitbox dos 6 minicubos; ganha o mais próximo da câmera.
    float bestT = 1e9f;
    int bestAxis = -1;
    for (int axis = 0; axis < 3; ++axis) {
      for (int sign = -1; sign <= 1; sign += 2) {
        const float half = (axis == 0 ? block.size.x : axis == 1 ? block.size.y : block.size.z) / 2.f;
        const float center = sign * (half + gap + cube / 2.f);
        Vec3 bmin = block.position, bmax = block.position;
        if (axis == 0) { bmin.x += center - cube / 2; bmax.x += center + cube / 2; bmin.y -= cube / 2; bmax.y += cube / 2; bmin.z -= cube / 2; bmax.z += cube / 2; }
        if (axis == 1) { bmin.y += center - cube / 2; bmax.y += center + cube / 2; bmin.x -= cube / 2; bmax.x += cube / 2; bmin.z -= cube / 2; bmax.z += cube / 2; }
        if (axis == 2) { bmin.z += center - cube / 2; bmax.z += center + cube / 2; bmin.x -= cube / 2; bmax.x += cube / 2; bmin.y -= cube / 2; bmax.y += cube / 2; }
        float t = 0;
        Vec3 normal;
        if (rayAabb(eye, dir, bmin, bmax, t, normal) && t < bestT) {
          bestT = t;
          bestAxis = axis;
        }
      }
    }
    if (bestAxis < 0) return;  // clique fora dos minicubos: nada acontece

    gizmoDragging_ = true;
    gizmoAxis_ = bestAxis;
    gizmoFaceSign_ = (dot(eye + mul(dir, bestT), axes[bestAxis]) >= dot(block.position, axes[bestAxis])) ? 1.f : -1.f;
    gizmoBlockStart_ = block.position;
    gizmoBlockStartSize_ = block.size;
    selStartPos_.clear();
    selStartSize_.clear();
    for (const int index : selection_) {
      if (index >= 0 && index < static_cast<int>(world_.blocks.size())) {
        selStartPos_.push_back(world_.blocks[static_cast<size_t>(index)].position);
        selStartSize_.push_back(world_.blocks[static_cast<size_t>(index)].size);
      } else {
        selStartPos_.push_back({});
        selStartSize_.push_back({});
      }
    }
    gizmoRayDist_ = bestT;                       // distância fixa do arrasto
    gizmoStartDot_ = dot(eye + mul(dir, bestT) - eye, axes[bestAxis]);  // referência no eixo
    pushUndo();
    setStatus(std::string("Arrastando eixo ") + "XYZ"[bestAxis] + " — solte para aplicar.");
    return;
  }

  if (!gizmoDragging_) return;
  if (!held) {
    gizmoDragging_ = false;
    renderer_.upload(world_);
    return;
  }

  // Arrasto: ponto do raio à MESMA distância do clique; o delta no eixo
  // (menos a referência inicial) move/estica os blocos de forma estável.
  const Vec3 eye = eyePos();
  const Vec3 dir = mouseRay(eye);
  const Vec3 rayPoint = eye + mul(dir, gizmoRayDist_);
  const float delta = dot(rayPoint - eye, axes[gizmoAxis_]) - gizmoStartDot_;

  // Função que aplica Move/Scale de UM bloco, dado seu estado inicial.
  auto transformOne = [&](Block& target, const Vec3& startPos, const Vec3& startSize) {
    if (tool_ == 2) {
      // Move: desloca no eixo pela diferença do parâmetro do mouse.
      target.position = startPos + mul(axes[gizmoAxis_], delta);
    } else {
      // Escala pela face arrastada; a face oposta fica fixa.
      Vec3 size = startSize;
      Vec3 pos = startPos;
      const float grow = delta * gizmoFaceSign_;
      const float halfOld = (gizmoAxis_ == 0 ? size.x : gizmoAxis_ == 1 ? size.y : size.z) / 2.f;
      const float halfNew = std::max(0.05f, halfOld + grow / 2.f);
      const float centerShift = (halfNew - halfOld) * gizmoFaceSign_;
      if (gizmoAxis_ == 0) { size.x = halfNew * 2.f; pos.x += centerShift; }
      else if (gizmoAxis_ == 1) { size.y = halfNew * 2.f; pos.y += centerShift; }
      else { size.z = halfNew * 2.f; pos.z += centerShift; }
      target.size = size;
      target.position = pos;
    }
    if (snapEnabled_) {
      target.position.x = std::round(target.position.x);
      target.position.y = std::round(target.position.y);
      target.position.z = std::round(target.position.z);
      if (tool_ == 3) {
        target.size.x = std::max(0.1f, std::round(target.size.x));
        target.size.y = std::max(0.1f, std::round(target.size.y));
        target.size.z = std::max(0.1f, std::round(target.size.z));
      }
    }
  };

  if (tool_ == 2) {
    // Move o CONJUNTO: todos os selecionados deslizam juntos.
    size_t cursor = 0;
    for (const int index : selection_) {
      if (index < 0 || index >= static_cast<int>(world_.blocks.size())) continue;
      transformOne(world_.blocks[static_cast<size_t>(index)], selStartPos_[cursor], selStartSize_[cursor]);
      ++cursor;
    }
  } else {
    // Escala conjunta: o âncora escala pela face; os outros ganham a MESMA
    // variação de largura no eixo (mantendo posições relativas no eixo).
    transformOne(block, gizmoBlockStart_, gizmoBlockStartSize_);
    const float widthAnchorBefore = (gizmoAxis_ == 0 ? gizmoBlockStartSize_.x : gizmoAxis_ == 1 ? gizmoBlockStartSize_.y : gizmoBlockStartSize_.z);
    const float widthAnchorAfter = (gizmoAxis_ == 0 ? block.size.x : gizmoAxis_ == 1 ? block.size.y : block.size.z);
    const float extraPerSide = (widthAnchorAfter - widthAnchorBefore) / 2.f;
    size_t cursor = 0;
    for (const int index : selection_) {
      if (index == selected_) { ++cursor; continue; }
      if (index < 0 || index >= static_cast<int>(world_.blocks.size())) continue;
      Block& member = world_.blocks[static_cast<size_t>(index)];
      member.size = selStartSize_[cursor];
      member.position = selStartPos_[cursor];
      if (gizmoAxis_ == 0) { member.size.x = std::max(0.1f, member.size.x + extraPerSide * 2.f); }
      else if (gizmoAxis_ == 1) { member.size.y = std::max(0.1f, member.size.y + extraPerSide * 2.f); }
      else { member.size.z = std::max(0.1f, member.size.z + extraPerSide * 2.f); }
      ++cursor;
    }
  }
  renderer_.upload(world_);
  markDirty();
}

// ---------------------------------------------------------------------------
// Prévias 3D dos templates na welcome screen (viewport por card via scissor)
// ---------------------------------------------------------------------------

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

void EditorApp::loadRecents() {
  recentList_.clear();
  std::ifstream file(recentsPath());
  std::string line;
  while (std::getline(file, line) && recentList_.size() < 8) {
    if (!line.empty() && std::filesystem::exists(line)) recentList_.push_back(line);
  }
}

void EditorApp::saveRecents() {
  std::ofstream file(recentsPath());
  for (const std::string& path : recentList_) file << path << '\n';
}

void EditorApp::addRecent(const std::string& path) {
  // Remove duplicado e coloca no topo.
  recentList_.erase(std::remove(recentList_.begin(), recentList_.end(), path), recentList_.end());
  recentList_.insert(recentList_.begin(), path);
  if (recentList_.size() > 8) recentList_.resize(8);
  saveRecents();
}

/** Pede o caminho (zenity) e salva. Devolve false se cancelado. */
bool EditorApp::saveWithDialog() {
  FILE* pipe = popen("zenity --file-selection --save --confirm-overwrite --title='Salvar mapa .loom' 2>/dev/null | tr -d '\\n'", "r");
  char buffer[1024] = {};
  bool ok = false;
  if (pipe && fgets(buffer, sizeof(buffer), pipe)) {
    std::string target = buffer;
    if (!target.empty()) {
      if (target.find(".loom") == std::string::npos) target += ".loom";
      std::string err;
      ok = saveLoom(target, err);
      if (!ok) setStatus("Erro ao salvar: " + err);
      else addRecent(target);
    }
  }
  if (pipe) pclose(pipe);
  return ok;
}

/** Cria uma cópia do mapa em um novo arquivo. */
void EditorApp::saveCopy() {
  const std::string backup = filePath_;
  filePath_.clear();  // força o diálogo perguntar onde
  if (!saveWithDialog()) {
    filePath_ = backup;  // cancelado: mantém o original
    setStatus("Cópia cancelada.");
  }
}

/** Renomeia o mapa (nome exibido) direto pela linha de comando do zenity. */
void EditorApp::renameMap() {
  std::string command = "zenity --entry --title='Renomear mapa' --text='Novo nome:' --entry-text='" + world_.name + "' 2>/dev/null | tr -d '\\n'";
  FILE* pipe = popen(command.c_str(), "r");
  char buffer[256] = {};
  if (pipe && fgets(buffer, sizeof(buffer), pipe)) {
    const std::string newName = buffer;
    if (!newName.empty()) {
      pushUndo();
      world_.name = newName;
      markDirty();
      setStatus("Mapa renomeado para \"" + newName + "\".");
    }
  }
  if (pipe) pclose(pipe);
}

/** Volta para a welcome screen (pergunta antes se há alterações). */
void EditorApp::backToWelcome() {
  if (dirty_) {
    confirmExit_ = true;
    return;
  }
  inEditor_ = false;
  setStatus("Bem-vindo ao Loomia Studio!");
}

/** Modal: "salvar antes de sair/voltar?" — 3 opções. */
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

}  // namespace loomia
