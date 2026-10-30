// Mini-client de teste dentro do editor: jogue o mapa no próprio viewport,
// sem login e sem rede. Controles de plataforma 3D simples (WASD + espaço),
// com física básica de gravidade/colisão contra os AABBs do mundo.
//
// É uma prévia de jogabilidade — o jogo de verdade é o cliente LOVR; aqui o
// objetivo é validar o mapa (spawn, plataformas, colisão) antes de publicar.
#include "editor.h"

#include <glad/glad.h>

#include <cmath>
#include <vector>

namespace loomia {

namespace {

std::vector<SimpleColliderBox> buildColliders(const World& world) {
  std::vector<SimpleColliderBox> out;
  out.reserve(world.blocks.size());
  for (const Block& block : world.blocks) {
    if (!block.collidable) continue;
    Vec3 bmin, bmax;
    World::bounds(block, bmin, bmax);
    out.push_back({bmin, bmax});
  }
  return out;
}

// Movimento com colisão simples por eixo (mundo de AABBs).
void movePlayer(Vec3& position, Vec3& velocity, bool& grounded, const std::vector<SimpleColliderBox>& colliders, const Vec3& delta) {
  const float radius = 0.4f, height = 1.8f;
  auto collides = [&](const Vec3& pos) {
    const Vec3 pmin{pos.x - radius, pos.y, pos.z - radius};
    const Vec3 pmax{pos.x + radius, pos.y + height, pos.z + radius};
    for (const SimpleColliderBox& collider : colliders) {
      if (pmax.x <= collider.min.x || pmin.x >= collider.max.x) continue;
      if (pmax.y <= collider.min.y || pmin.y >= collider.max.y) continue;
      if (pmax.z <= collider.min.z || pmin.z >= collider.max.z) continue;
      return true;
    }
    return false;
  };

  // eixo por eixo
  const Vec3 step = delta;
  Vec3 candidate = position + Vec3{step.x, 0, 0};
  if (!collides(candidate)) position = candidate;
  candidate = position + Vec3{0, 0, step.z};
  if (!collides(candidate)) position = candidate;
  candidate = position + Vec3{0, step.y, 0};
  if (!collides(candidate)) {
    position = candidate;
    grounded = false;
  } else {
    if (step.y < 0) grounded = true;
    velocity.y = 0;
  }
}

}  // namespace

// Play no próprio editor: física local + avatar. Chama por frame no lugar da
// edição quando playTestOpen_ está ativo.
void EditorApp::updateTestPlay(float dt) {
  if (!playTestOpen_ || testPaused_) return;

  // Estado do teste (recria ao entrar).
  if (!testPlayActive_) {
    testPlayActive_ = true;
    testPlayerPos_ = {0, 2, 0};
    testPlayerVel_ = {0, 0, 0};
    testPlayerGrounded_ = false;
    testColliders_ = buildColliders(world_);
    testPlayerPos_ = world_.spawn;
    setStatus("Modo teste: WASD anda, espaço pula, Esc volta ao editor.");
  }

  Vec3& position = testPlayerPos_;
  Vec3& velocity = testPlayerVel_;
  bool& grounded = testPlayerGrounded_;

  // Câmera em 3ª pessoa atrás do jogador.
  const float speed = 6.f * dt;
  float forwardX = 0, forwardZ = 0;
  if (keys_['W']) { forwardX += std::sin(camYaw_); forwardZ += std::cos(camYaw_); }
  if (keys_['S']) { forwardX -= std::sin(camYaw_); forwardZ -= std::cos(camYaw_); }
  if (keys_['A']) { forwardX += std::cos(camYaw_); forwardZ -= std::sin(camYaw_); }
  if (keys_['D']) { forwardX -= std::cos(camYaw_); forwardZ += std::sin(camYaw_); }
  float length = std::hypot(forwardX, forwardZ);
  if (length > 0) { forwardX /= length; forwardZ /= length; }

  velocity.y -= 22.f * dt;  // gravidade
  if (keys_[32] /*espaço*/ && grounded) {
    velocity.y = 9.f;
    grounded = false;
  }

  const Vec3 delta{forwardX * speed, velocity.y * dt, forwardZ * speed};
  movePlayer(position, velocity, grounded, testColliders_, delta);

  // Caiu do mundo: volta ao spawn.
  if (position.y < -40) {
    position = world_.spawn;
    velocity = {0, 0, 0};
  }

  // Câmera segue o jogador.
  camTarget_ = position + Vec3{0, 1.4f, 0};
}

// Sai do modo teste (Esc).
void EditorApp::pauseTestPlay() {
  if (!playTestOpen_ || !testPlayActive_) return;
  testPaused_ = true;
  setStatus("Teste pausado — o estado do jogador foi preservado.");
}

void EditorApp::resumeTestPlay() {
  if (!playTestOpen_ || !testPlayActive_) return;
  testPaused_ = false;
  setStatus("Teste retomado.");
}

void EditorApp::restartTestPlay() {
  if (!playTestOpen_) return;
  testPlayerPos_ = world_.spawn;
  testPlayerVel_ = {0, 0, 0};
  testPlayerGrounded_ = false;
  testPaused_ = false;
  luaConsole_ = "test: reiniciado\n";
  setStatus("Teste reiniciado do spawn.");
}

void EditorApp::endTestPlay() {
  if (!playTestOpen_) return;
  playTestOpen_ = false;
  testPlayActive_ = false;
  // Restaura a câmera de edição (aproximada) — o mundo nunca foi alterado.
  camTarget_ = {0, 2, 0};
  camDist_ = 34;
  setStatus("De volta ao editor — nada foi perdido.");
}

}  // namespace loomia
