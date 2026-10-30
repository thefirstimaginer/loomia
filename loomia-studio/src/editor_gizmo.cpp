// Gizmos dos minicubos: picking (6 hitboxes) e arrasto com raio de distância
// fixa — move o conjunto ou escala pela face com a oposta fixa.
#include "editor.h"

#include <algorithm>
#include <cmath>

#include "renderer.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace loomia {
bool EditorApp::updateGizmoTryPick(double mx, double my) {
  if (selected_ < 0 || selected_ >= static_cast<int>(world_.blocks.size())) return false;
  if (selected_ < static_cast<int>(blockLocked_.size()) && blockLocked_[static_cast<size_t>(selected_)]) return false;
  const Block& block = world_.blocks[static_cast<size_t>(selected_)];

  const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  // Tamanho do gizmo proporcional à distância da câmera: na tela o minicubo
  // fica sempre com o mesmo tamanho (antes, longe do objeto ele encolhia
  // alguns pixels e ficava impossível de clicar).
  const float gizmoScale = std::max(0.6f, camDist_ * 0.035f);
  const float gap = gizmoScale * 0.9f;
  const float cube = gizmoScale * 0.55f;

  const Vec3 eye{
      camTarget_.x + camDist_ * std::cos(camPitch_) * std::sin(camYaw_),
      camTarget_.y + camDist_ * std::sin(camPitch_),
      camTarget_.z + camDist_ * std::cos(camPitch_) * std::cos(camYaw_)};
  const LayoutRects lay = layout();
  const float vw = static_cast<float>(lay.viewportW()), vh = static_cast<float>(lay.viewportH());
  const float localX = static_cast<float>(mx - lay.viewportX());
  const float localY = static_cast<float>(my - lay.viewportY());
  const float aspect = vw / (vh > 0 ? vh : 1);
  const float ndcX = localX / vw * 2.f - 1.f;
  const float ndcY = 1.f - localY / vh * 2.f;
  const float tanHalf = std::tan(radians(35.f));
  Vec3 forward = normalize(sub(camTarget_, eye));
  Vec3 right = normalize(cross(forward, {0, 1, 0}));
  Vec3 up = cross(right, forward);
  Vec3 dir = normalize(forward + mul(right, ndcX * tanHalf * aspect) + mul(up, ndcY * tanHalf));

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
  // Fallback em espaço de tela: projeta o centro de cada minicubo e aceita o
  // clique dentro de um raio de pixels — cobre ângulos em que o raio passa
  // raspando (o "às vezes funciona, às vezes não").
  if (bestAxis < 0) {
    float bestPx = 14.f;  // raio de tolerância
    int bestScreen = -1;
    for (int axis = 0; axis < 3; ++axis) {
      for (int sign = -1; sign <= 1; sign += 2) {
        const float half = (axis == 0 ? block.size.x : axis == 1 ? block.size.y : block.size.z) / 2.f;
        const float center = sign * (half + gap + cube / 2.f);
        Vec3 c = block.position;
        if (axis == 0) c.x += center;
        if (axis == 1) c.y += center;
        if (axis == 2) c.z += center;
        Vec3 toCube = c - eye;
        const float depth = dot(toCube, forward);
        if (depth <= 0.1f) continue;
        const Vec3 onPlane = mul(forward, depth);
        const Vec3 flat = toCube - onPlane;
        const float sx = fbw_ / 2.f + (dot(flat, right) / (depth * tanHalf * aspect)) * (fbw_ / 2.f);
        const float sy = fbh_ / 2.f - (dot(flat, up) / (depth * tanHalf)) * (fbh_ / 2.f);
        const float distPx = std::hypot(static_cast<float>(mx) - sx, static_cast<float>(my) - sy);
        if (distPx < bestPx) { bestPx = distPx; bestScreen = axis; }
      }
    }
    if (bestScreen < 0) return false;
    bestAxis = bestScreen;
    // Distância de arrasto: profundidade do centro do bloco (estável).
    bestT = dot(block.position - eye, forward);
  }

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
  gizmoRayDist_ = bestT;
  gizmoStartDot_ = dot(eye + mul(dir, bestT) - eye, axes[bestAxis]);
  pushUndo();
  setStatus(std::string("Arrastando eixo ") + "XYZ"[bestAxis] + " — solte para aplicar.");
  return true;
}

void EditorApp::updateGizmoDrag(double mx, double my, bool held) {
  if (!gizmoDragging_) return;
  if (selected_ < 0 || selected_ >= static_cast<int>(world_.blocks.size())) {
    gizmoDragging_ = false;
    return;
  }
  Block& block = world_.blocks[static_cast<size_t>(selected_)];

  const Vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

  if (!held) {
    gizmoDragging_ = false;
    renderer_.upload(world_);
    return;
  }

  // Ponto do raio à distância fixa do clique → delta estável no eixo.
  const Vec3 eye{
      camTarget_.x + camDist_ * std::cos(camPitch_) * std::sin(camYaw_),
      camTarget_.y + camDist_ * std::sin(camPitch_),
      camTarget_.z + camDist_ * std::cos(camPitch_) * std::cos(camYaw_)};
  const float aspect = static_cast<float>(fbw_) / static_cast<float>(fbh_ > 0 ? fbh_ : 1);
  const float ndcX = static_cast<float>(mx) / fbw_ * 2.f - 1.f;
  const float ndcY = 1.f - static_cast<float>(my) / fbh_ * 2.f;
  const float tanHalf = std::tan(radians(35.f));
  Vec3 forward = normalize(sub(camTarget_, eye));
  Vec3 right = normalize(cross(forward, {0, 1, 0}));
  Vec3 up = cross(right, forward);
  Vec3 dir = normalize(forward + mul(right, ndcX * tanHalf * aspect) + mul(up, ndcY * tanHalf));
  const Vec3 rayPoint = eye + mul(dir, gizmoRayDist_);
  const float delta = dot(rayPoint - eye, axes[gizmoAxis_]) - gizmoStartDot_;

  // Move/escala de um bloco dado o estado inicial.
  auto transformOne = [&](Block& target, const Vec3& startPos, const Vec3& startSize) {
    if (tool_ == 2) {
      target.position = startPos + mul(axes[gizmoAxis_], delta);
    } else {
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
    // Move: o conjunto inteiro desliza junto.
    size_t cursor = 0;
    for (const int index : selection_) {
      if (index < 0 || index >= static_cast<int>(world_.blocks.size())) continue;
      transformOne(world_.blocks[static_cast<size_t>(index)], selStartPos_[cursor], selStartSize_[cursor]);
      ++cursor;
    }
  } else {
    // Escala: âncora pela face; demais recebem a mesma variação de largura.
    transformOne(block, gizmoBlockStart_, gizmoBlockStartSize_);
    const float widthBefore = (gizmoAxis_ == 0 ? gizmoBlockStartSize_.x : gizmoAxis_ == 1 ? gizmoBlockStartSize_.y : gizmoBlockStartSize_.z);
    const float widthAfter = (gizmoAxis_ == 0 ? block.size.x : gizmoAxis_ == 1 ? block.size.y : block.size.z);
    const float extraPerSide = (widthAfter - widthBefore) / 2.f;
    size_t cursor = 0;
    for (const int index : selection_) {
      if (index == selected_) { ++cursor; continue; }
      if (index < 0 || index >= static_cast<int>(world_.blocks.size())) continue;
      Block& member = world_.blocks[static_cast<size_t>(index)];
      member.size = selStartSize_[cursor];
      member.position = selStartPos_[cursor];
      if (gizmoAxis_ == 0) member.size.x = std::max(0.1f, member.size.x + extraPerSide * 2.f);
      else if (gizmoAxis_ == 1) member.size.y = std::max(0.1f, member.size.y + extraPerSide * 2.f);
      else member.size.z = std::max(0.1f, member.size.z + extraPerSide * 2.f);
      ++cursor;
    }
  }
  renderer_.upload(world_);
  markDirty();
}

// ---------------------------------------------------------------------------
// Prévias 3D dos templates na welcome screen (viewport por card via scissor)
// ---------------------------------------------------------------------------


}  // namespace loomia
