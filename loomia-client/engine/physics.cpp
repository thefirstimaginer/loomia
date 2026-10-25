#include "physics.h"

#include <algorithm>
#include <cmath>

namespace loomia {

std::vector<Collider> buildColliders(const World& world) {
  std::vector<Collider> colliders;
  colliders.reserve(world.blocks.size());
  for (const Block& block : world.blocks) {
    if (!block.collidable) continue;
    Collider collider;
    World::bounds(block, collider.min, collider.max);
    colliders.push_back(collider);
  }
  return colliders;
}

float worldMinY(const World& world) {
  float lowest = 0;
  bool first = true;
  for (const Block& block : world.blocks) {
    if (!block.collidable) continue;
    Vec3 min, max;
    World::bounds(block, min, max);
    if (first || min.y < lowest) {
      lowest = min.y;
      first = false;
    }
  }
  return lowest - 40.0f;
}

bool highestBelow(const std::vector<Collider>& colliders, float x, float y, float z, float probe,
                  float& surface) {
  const float radius = PhysicsConstants::characterRadius - 0.01f;
  float best = 0;
  bool found = false;
  for (const Collider& box : colliders) {
    if (x + radius <= box.min.x || x - radius >= box.max.x) continue;
    if (z + radius <= box.min.z || z - radius >= box.max.z) continue;
    if (box.max.y > y + 0.02f || box.max.y < y - probe) continue;
    if (!found || box.max.y > best) {
      best = box.max.y;
      found = true;
    }
  }
  surface = best;
  return found;
}

namespace {

bool solidAt(const std::vector<Collider>& colliders, float x, float y, float z) {
  const float radius = PhysicsConstants::characterRadius;
  for (const Collider& box : colliders) {
    if (x + radius <= box.min.x || x - radius >= box.max.x) continue;
    if (z + radius <= box.min.z || z - radius >= box.max.z) continue;
    if (y >= box.max.y || y + PhysicsConstants::characterHeight <= box.min.y) continue;
    return true;
  }
  return false;
}

}  // namespace

CharacterState step(const CharacterState& state, const MoveInput& input,
                    const std::vector<Collider>& colliders, float dt) {
  CharacterState s = state;

  float moveX = clampf(input.moveX, -1, 1);
  float moveZ = clampf(input.moveZ, -1, 1);
  const float lengthXY = std::sqrt(moveX * moveX + moveZ * moveZ);
  if (lengthXY > 1) {
    moveX /= lengthXY;
    moveZ /= lengthXY;
  }
  s.velocity.x = moveX * PhysicsConstants::moveSpeed;
  s.velocity.z = moveZ * PhysicsConstants::moveSpeed;
  if (lengthXY > 0.01f) s.yaw = std::atan2(moveX, moveZ);

  const bool wasGrounded = s.onGround;
  s.onGround = false;
  if (input.jump && wasGrounded) {
    s.velocity.y = PhysicsConstants::jumpSpeed;
  } else {
    s.velocity.y += PhysicsConstants::gravity * dt;
    if (s.velocity.y < PhysicsConstants::maxFallSpeed) s.velocity.y = PhysicsConstants::maxFallSpeed;
  }

  float px = s.position.x, py = s.position.y, pz = s.position.z;
  float vx = s.velocity.x, vy = s.velocity.y, vz = s.velocity.z;

  // --- Horizontal, eixo a eixo (desliza nas paredes). ---
  if (!solidAt(colliders, px + vx * dt, py, pz)) {
    px += vx * dt;
  } else {
    float ground = 0;
    if (highestBelow(colliders, px + vx * dt, py + PhysicsConstants::stepHeight, pz,
                     PhysicsConstants::stepHeight, ground) &&
        !solidAt(colliders, px + vx * dt, ground, pz)) {
      px += vx * dt;
      py = ground;
    } else {
      vx = 0;
    }
  }
  if (!solidAt(colliders, px, py, pz + vz * dt)) {
    pz += vz * dt;
  } else {
    float ground = 0;
    if (highestBelow(colliders, px, py + PhysicsConstants::stepHeight, pz + vz * dt,
                     PhysicsConstants::stepHeight, ground) &&
        !solidAt(colliders, px, ground, pz + vz * dt)) {
      pz += vz * dt;
      py = ground;
    } else {
      vz = 0;
    }
  }

  // --- Vertical. ---
  float nextY = py + vy * dt;
  if (vy <= 0) {
    float ground = 0;
    if (highestBelow(colliders, px, py, pz, py - nextY + 0.02f, ground)) {
      nextY = ground;
      vy = 0;
      s.onGround = true;
    }
  } else if (solidAt(colliders, px, nextY, pz)) {
    vy = 0;
    nextY = py;
  }
  py = nextY;

  if (!s.onGround && std::fabs(vy) < 1.0f) {
    float ground = 0;
    if (highestBelow(colliders, px, py, pz, PhysicsConstants::snapDistance, ground)) {
      py = ground;
      vy = 0;
      s.onGround = true;
    }
  }

  s.position = {px, py, pz};
  s.velocity = {vx, vy, vz};
  return s;
}

}  // namespace loomia
