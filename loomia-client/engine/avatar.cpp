#include "avatar.h"

#include <cmath>

#include "json.h"

namespace loomia {

namespace {

Color3 readColor(const json::ValuePtr& value, const Color3& fallback) {
  if (!value || !value->isArray() || value->size() < 3) return fallback;
  Color3 color;
  color.r = static_cast<uint8_t>(clampf(static_cast<float>(value->at(0)), 0, 255));
  color.g = static_cast<uint8_t>(clampf(static_cast<float>(value->at(1)), 0, 255));
  color.b = static_cast<uint8_t>(clampf(static_cast<float>(value->at(2)), 0, 255));
  return color;
}

json::ValuePtr writeColor(const Color3& color) { return json::makeVec3(color.r, color.g, color.b); }

}  // namespace

Appearance Appearance::fromJson(const std::string& text) {
  Appearance look;
  const json::ValuePtr root = json::parse(text);
  if (!root) return look;
  look.username = root->str("username", root->str("name", "Loomian"));
  look.body = readColor(root->get("body_color"), look.body);
  look.shirt = readColor(root->get("shirt_color"), look.shirt);
  look.pants = readColor(root->get("pants_color"), look.pants);
  look.hat = root->str("hat", "");
  return look;
}

std::string Appearance::toJson() const {
  auto root = json::makeObject();
  json::set(*root, "username", json::makeString(username));
  json::set(*root, "body_color", writeColor(body));
  json::set(*root, "shirt_color", writeColor(shirt));
  json::set(*root, "pants_color", writeColor(pants));
  if (!hat.empty()) json::set(*root, "hat", json::makeString(hat));
  return json::writeCompact(*root);
}

void Avatar::setup(const Appearance& look) {
  look_ = look;
  name_ = look.username;
  clock_ = 0;
}

void Avatar::update(const Vec3& position, float yaw, float speed, float dt) {
  position_ = position;
  yaw_ = yaw;
  speed_ = speed;
  if (speed > 0.5f) {
    // Mesmo ritmo do cliente de referência (passada ~1,4 ciclo/s andando).
    clock_ += dt * (3.0f + speed * 0.35f);
  }
}

Mat4 Avatar::root(float swing) const {
  (void) swing;
  return Mat4::translation(position_) * Mat4::rotationY(yaw_);
}

void Avatar::draw(WorldRenderer& renderer, const Camera& camera, float aspect) const {
  const float swing = speed_ > 0.5f ? std::sin(clock_) * 0.42f : 0.0f;
  const Mat4 base = root(swing);

  // Posiciona cada parte (centro + tamanho) no espaço do avatar.
  // A ordem importa: primeiro reposiciona o centro, depois escala em torno
  // dele — escalar antes inflaria também o deslocamento (bug visual das
  // partes flutuando longe do corpo).
  auto box = [&](const Vec3& center, const Vec3& size, const Color3& color, bool emissive = false) {
    const Mat4 model = base * Mat4::translation(center) * Mat4::scale(size);
    renderer.drawBox(camera, aspect, model, color, emissive);
  };

  // Pernas.
  box(Vec3{-0.25f, 0.5f, 0}, Vec3{0.5f, 1.0f, 0.5f}, look_.pants);
  box(Vec3{0.25f, 0.5f, 0}, Vec3{0.5f, 1.0f, 0.5f}, look_.pants);

  // Torso e cabeça.
  box(Vec3{0, 1.7f, 0}, Vec3{1.0f, 1.4f, 0.6f}, look_.shirt);
  box(Vec3{0, 3.0f, 0}, Vec3{1.22f, 1.22f, 1.22f}, look_.body);

  // Braços (com leve balanceio).
  box(Vec3{-0.75f, 2.3f + swing * 0.08f, 0}, Vec3{0.4f, 1.0f, 0.6f}, look_.shirt);
  box(Vec3{0.75f, 2.3f - swing * 0.08f, 0}, Vec3{0.4f, 1.0f, 0.6f}, look_.shirt);

  // Mãos.
  box(Vec3{-0.75f, 1.72f + swing * 0.08f, 0}, Vec3{0.4f, 0.4f, 0.45f}, look_.body);
  box(Vec3{0.75f, 1.72f - swing * 0.08f, 0}, Vec3{0.4f, 0.4f, 0.45f}, look_.body);

  // Olhos.
  box(Vec3{-0.22f, 3.08f, 0.62f}, Vec3{0.16f, 0.16f, 0.06f}, Color3{25, 25, 30});
  box(Vec3{0.22f, 3.08f, 0.62f}, Vec3{0.16f, 0.16f, 0.06f}, Color3{25, 25, 30});

  // Chapéu.
  if (look_.hat == "cap") {
    box(Vec3{0, 3.75f, 0}, Vec3{1.3f, 0.3f, 1.3f}, Color3{200, 45, 45});
    box(Vec3{0, 3.62f, 0.78f}, Vec3{1.15f, 0.12f, 0.5f}, Color3{160, 30, 30});
  } else if (look_.hat == "top_hat") {
    box(Vec3{0, 3.68f, 0}, Vec3{1.6f, 0.15f, 1.6f}, Color3{20, 20, 26});
    box(Vec3{0, 4.2f, 0}, Vec3{1.0f, 1.0f, 1.0f}, Color3{20, 20, 26});
  } else if (look_.hat == "crown") {
    box(Vec3{0, 3.8f, 0}, Vec3{1.3f, 0.3f, 1.3f}, Color3{242, 199, 38}, true);
    box(Vec3{-0.4f, 4.02f, 0}, Vec3{0.16f, 0.25f, 0.16f}, Color3{242, 199, 38}, true);
    box(Vec3{0, 4.02f, 0}, Vec3{0.16f, 0.25f, 0.16f}, Color3{242, 199, 38}, true);
    box(Vec3{0.4f, 4.02f, 0}, Vec3{0.16f, 0.25f, 0.16f}, Color3{242, 199, 38}, true);
  } else if (look_.hat == "propeller") {
    box(Vec3{0, 3.75f, 0}, Vec3{1.3f, 0.3f, 1.3f}, Color3{250, 200, 40});
    box(Vec3{0, 4.05f, 0}, Vec3{1.4f, 0.08f, 0.2f}, Color3{60, 150, 220}, true);
  }
}

}  // namespace loomia
