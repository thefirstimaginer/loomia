// Definição completa do EditorApp — compartilhada pelos módulos do studio.
//
// O código é dividido por responsabilidade:
//   editor.cpp        — init da janela, loop principal, picking de blocos
//   editor_ui.cpp     — welcome screen, toolbar, menus, status, modal
//   editor_gizmo.cpp  — minicubos de face: picking e arrasto (move/escala)
//   editor_actions.cpp— arquivos (abrir/salvar/recentes/renomear), undo/redo,
//                       seleção (simples e múltipla), ações de edição
#pragma once

#include "font.h"
#include "templates.h"

#include "renderer.h"
#include "world.h"
#include "loom_format.h"
#include "theme.h"
#include "icon_atlas.h"

#include <string>
#include <vector>

struct GLFWwindow;

namespace loomia {

enum class EditorResult { Quit, OpenAgain };

struct SimpleColliderBox { Vec3 min, max; };

/** Roda o app inteiro (welcome -> editor). Bloqueia até o usuário sair. */
EditorResult runStudio();

// Overlay 2D (rects/frame/triângulos) — desenhado sobre a cena 3D.
struct UiOverlay {
  unsigned int vao = 0, vbo = 0;
  ShaderProgram shader;
  std::vector<float> verts;  // x,y,r,g,b,a por vértice
  int width = 1440, height = 860;
  bool ready = false;

  bool init(std::string& error);
  void destroy();
  void tri(float x0, float y0, float x1, float y1, float x2, float y2, float r, float g, float b, float a = 1.0f);
  void rect(float x, float y, float w, float h, float r, float g, float b, float a = 1.0f);
  void frame(float x, float y, float w, float h, float t, float r, float g, float b, float a = 1.0f);
  void text(const std::string& str, float x, float y, float cell, float r, float g, float b, float a = 1.0f);
  float textWidth(const std::string& str, float cell) const;
  void flush();
};

// Hit de picking (bloco ou chão).
struct RayHit {
  bool hit = false;
  Vec3 point;
  float distance = 0;
  int blockIndex = -1;  // -1 = chão/grade
  Vec3 normal;
};

inline Vec3 sub(const Vec3& a, const Vec3& b) { return a - b; }
inline Vec3 mul(const Vec3& a, float s) { return a * s; }

// Raio vs AABB (usado pelos gizmos).
bool rayAabb(const Vec3& origin, const Vec3& dir, const Vec3& bmin, const Vec3& bmax, float& outT, Vec3& outNormal);
bool rayPlaneY(const Vec3& origin, const Vec3& dir, float planeY, float& outT);

class EditorApp {
 public:
  EditorResult run();
  EditorApp();
  ~EditorApp();

  // ---- estado compartilhado (módulos leem/escrevem) ----
  GLFWwindow* window_ = nullptr;
  WorldRenderer renderer_;
  UiOverlay ui_;
  UiFont font_;
  std::string fontError_;

  World world_;
  LoomDocument loomDoc_;   // documento v1 (scripts/lógica preservados)
  std::string filePath_;
  bool dirty_ = false;
  bool inEditor_ = false;
  std::string status_;
  std::string modalMessage_;

  float camYaw_ = 0.8f, camPitch_ = 0.5f, camDist_ = 34.f;
  Vec3 camTarget_{0, 2, 0};

  int tool_ = 0;
  int selected_ = -1;
  int hover_ = -1;
  int currentMaterial_ = 4;
  int currentColor_[3] = {230, 120, 60};
  float currentSize_[3] = {1, 1, 1};
  bool snapEnabled_ = true;

  bool dragging_ = false, orbiting_ = false, panning_ = false;
  double lastX_ = 0, lastY_ = 0;
  bool clickConsumed_ = false;
  bool mouseWasDown_ = false;
  int uiCooldown_ = 0;           // frames ignorando cliques após um modal agir
  bool keys_[512] = {};
  int fbw_ = 1440, fbh_ = 860;

  std::vector<World> undoStack_, redoStack_;

  // gizmos
  int gizmoMode_ = 0;
  int gizmoAxis_ = -1;
  Vec3 gizmoBlockStart_{};
  Vec3 gizmoBlockStartSize_{};
  bool gizmoDragging_ = false;
  float gizmoFaceSign_ = 1.f;
  float gizmoRayDist_ = 0.f;
  float gizmoStartDot_ = 0.f;
  std::vector<Vec3> selStartPos_, selStartSize_;

  // seleção múltipla
  std::vector<int> selection_;
  bool marqueeActive_ = false;

  // Hierarquia avançada
  std::string hierarchyFilter_;       // busca
  std::vector<bool> blockHidden_;     // oculto no viewport (render skip)
  std::vector<bool> blockLocked_;     // bloqueado contra seleção/edição
  std::vector<std::string> blockGroups_;  // pasta/grupo do objeto
  // Mantém os vetores paralelos alinhados ao mundo (chamar após mutações).
  void syncBlockMeta();
  void eraseBlockMeta(int index);
  int hierarchyScroll_ = 0;

  void addObjectPrimitive(int shapeKind);   // 0=box 1=sphere 2=cylinder
  void addLight();
  void toggleHidden(int index);
  void toggleLocked(int index);
  void setGroup(int index, const std::string& group);

  // Autosave, histórico e assets
  void tickAutosave(float dt);
  void addHistory(const std::string& entry);
  void drawAssetBrowser(int fbw, int fbh, const Theme& th);
  float autosaveTimer_ = 0.f;
  std::vector<std::string> history_;
  int previewAspect_ = 0;   // 0=livre 1=16:9 2=4:3 3=1:1
  float marqueeStart_[2] = {0, 0};
  float marqueeNow_[2] = {0, 0};

  // menus
  int activeTab_ = 0;            // 0=Cena, 1=Scripts
  int sidebarWidth_ = 260;       // px, redimensionável
  bool prefsOpen_ = false;
  bool prefsDark_ = false;       // tema escuro da interface (não do mapa)
  bool prefsSnapAlways_ = true;
  bool prefsShowGrid_ = true;
  bool prefsShowGizmos_ = true;
  bool prefsInvertOrbit_ = false;
  float prefsOrbitSpeed_ = 0.8f;
  bool playTestOpen_ = false;    // janela de teste (Play)
  bool fileMenu_ = false;
  bool colorPicker_ = false;
  bool sceneMenu_ = false;
  float skyHue_ = 0.58f, skySat_ = 0.5f, skyVal_ = 0.95f;
  std::vector<std::string> recentList_;
  bool confirmExit_ = false;
  bool exitRequested_ = false;

  IconAtlas icons_;
  std::string iconsDir_;

  WorldRenderer previewRenderer_;
  bool previewReady_ = false;

  // mini-client de teste: estados play/pausa/stop
  bool testPlayActive_ = false;
  bool testPaused_ = false;
  Vec3 testPlayerPos_{0, 2, 0};
  Vec3 testPlayerVel_{0, 0, 0};
  bool testPlayerGrounded_ = false;
  std::vector<SimpleColliderBox> testColliders_;

  // Tema e layout responsivo (fix 1 e 2)
  Theme theme() const { return prefsDark_ ? Theme::dark() : Theme::light(); }
  Theme th_ = Theme::light();  // cache do frame para drawPrefs
  LayoutRects layout() const {
    LayoutRects r;
    r.fbw = fbw_;
    r.fbh = fbh_;
    r.rightW = sidebarWidth_;
    return r;
  }

  // ---- editor_shell.cpp (redesign 3D) ----
  void drawTopbar(int fbw, int fbh, const Theme& th);
  void drawToolRail(int fbw, int fbh, const Theme& th);
  void drawHierarchy(int fbw, int fbh, const Theme& th);
  void drawProperties(int fbw, int fbh, const Theme& th);
  void drawResourceStrip(int fbw, int fbh, const Theme& th);
  void drawViewportFrame(int fbw, int fbh, const Theme& th);
  void drawStatusLine(int fbw, int fbh, const Theme& th);

  // Editor Lua integrado (abas de script + realce + console)
  int luaEditorScript_ = -1;      // índice do script aberto (-1 nenhum)
  std::string luaConsole_;
  void drawLuaEditor(int fbw, int fbh, const Theme& th);
  void drawLuaCodeEditor(int fbw, int fbh, const Theme& th);
  void updateTestPlay(float dt);
  void endTestPlay();
  void pauseTestPlay();
  void resumeTestPlay();
  void restartTestPlay();
  std::string luaSearch_;

  // ---- editor.cpp ----
  bool initWindow(std::string& error);
  void shutdown();
  void updateCamera(float dt);
  RayHit pickBlock(double mouseX, double mouseY, int fbw, int fbh);
  void setStatus(const std::string& message);
  void markDirty();
  void pushUndo();
  void undo();
  void redo();

  // ---- editor_actions.cpp ----
  void newFromTemplate(int templateIndex);
  bool openLoom(const std::string& path, std::string& error);
  bool saveLoom(const std::string& path, std::string& error);
  void deleteSelection();
  void duplicateSelection();
  void focusSelection();
  void snapSelection();
  void loadRecents();
  void saveRecents();
  void addRecent(const std::string& path);
  bool saveWithDialog();
  void saveCopy();
  void renameMap();
  void backToWelcome();

  // ---- editor_testplay.cpp ----
  void runPlayTest();            // abre a janela de teste e bloqueia até fechar
  void drawScriptsPanel(int fbw, int fbh);   // sidebar da aba Scripts
  void drawScenePanel(int fbw, int fbh);     // sidebar da aba Cena
  int scriptPanelSel_ = -1;                  // script selecionado na sidebar
  void drawPrefs(int fbw, int fbh);         // painel de preferências
  void drawPlayWindow();                     // janela separada do Play

  // ---- editor_ui.cpp ----
  struct TextDraw { std::string str; float x, y, size, r, g, b, a; };
  void uiText(const std::string& str, float x, float y, float px, float r, float g, float b, float a = 1.0f);
  void flushText();
  std::vector<TextDraw> textQueue_;
  int buttonHit(double mx, double my, float x, float y, float w, float h) const;
  void drawUi(int fbw, int fbh);
  void drawWelcome(int fbw, int fbh);
  void drawSceneMenu(float fbw, float fbh);
  void drawConfirmExit(int fbw, int fbh);
  void drawTemplatePreviews(int fbw, int fbh);
  void hslToRgb(float h, float s, float v, float out[3]) const;

  // ---- editor_gizmo.cpp ----
  bool updateGizmoTryPick(double mx, double my);
  void updateGizmoDrag(double mx, double my, bool held);
};

// Constantes de layout compartilhadas.
constexpr int WINDOW_W = 1440;
constexpr int WINDOW_H = 860;
constexpr int UI_BAR_H = 64;
constexpr int UI_STATUS_H = 26;

}  // namespace loomia
