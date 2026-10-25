// Física local do personagem — ESPELHA loomia-gameserver/physics.go.
//
// O cliente prevê o movimento e reconcilia com o servidor. Se mudar aqui,
// mude lá (as constantes e a ordem de resolução são as mesmas).
#pragma once

#include <vector>

#include "math.h"
#include "world.h"

namespace loomia {

struct PhysicsConstants {
  static constexpr float tickRate = 30.0f;
  static constexpr float tickDt = 1.0f / tickRate;
  static constexpr float moveSpeed = 12.5f;
  static constexpr float jumpSpeed = 40.0f;
  static constexpr float gravity = -95.0f;
  static constexpr float maxFallSpeed = -90.0f;
  static constexpr float characterRadius = 0.55f;
  static constexpr float characterHeight = 3.0f;
  static constexpr float stepHeight = 0.6f;
  static constexpr float snapDistance = 0.55f;
  static constexpr float eyeHeight = 1.55f;
};

/** Caixa de colisão já calculada (só blocos sólidos). */
struct Collider {
  Vec3 min;
  Vec3 max;
};

struct CharacterState {
  Vec3 position{0, 0, 0};
  Vec3 velocity{0, 0, 0};
  float yaw = 0;
  bool onGround = false;
};

struct MoveInput {
  float moveX = 0;
  float moveZ = 0;
  bool jump = false;
};

/** Blocos sólidos do mundo pré-processados (evita recalcular a cada tique). */
std::vector<Collider> buildColliders(const World& world);

/** Piso de segurança: abaixo disso o jogador "caiu do mundo". */
float worldMinY(const World& world);

CharacterState step(const CharacterState& state, const MoveInput& input,
                    const std::vector<Collider>& colliders, float dt = PhysicsConstants::tickDt);

/** Ponto mais alto de apoio sob os pés (usado no step e no respawn). */
bool highestBelow(const std::vector<Collider>& colliders, float x, float y, float z, float probe,
                  float& surface);

}  // namespace loomia
