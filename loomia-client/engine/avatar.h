// Avatar do jogador: caixas no estilo 2008 + etiqueta de nome.
//
// A etiqueta é desenhada em 2D pelo launcher/jogo (projeção manual): assim não
// dependemos de texto 3D nem de fontes na GPU.
#pragma once

#include <string>

#include "math.h"
#include "renderer.h"

namespace loomia {

struct Appearance {
  std::string username;
  Color3 body{234, 190, 25};
  Color3 shirt{15, 120, 190};
  Color3 pants{35, 75, 45};
  std::string hat;  // "", "cap", "top_hat", "propeller", "crown"

  static Appearance fromJson(const std::string& json);
  std::string toJson() const;
};

class Avatar {
 public:
  /** Prepara as partes (não desenha nada). */
  void setup(const Appearance& look);

  /** Atualiza posição/yaw e a caminhada (oscila braços e pernas). */
  void update(const Vec3& position, float yaw, float speed, float dt);

  /** Desenha no renderer (usa drawBox por parte). */
  void draw(WorldRenderer& renderer, const Camera& camera, float aspect) const;

  const Vec3& position() const { return position_; }
  float yaw() const { return yaw_; }
  const std::string& name() const { return name_; }
  void setName(const std::string& name) { name_ = name; }

  /** Altura do topo da cabeça (para a etiqueta). */
  static constexpr float nameHeight = 4.15f;

 private:
  Mat4 root(float swing) const;

  Appearance look_;
  std::string name_;
  Vec3 position_{0, 0, 0};
  float yaw_ = 0;
  float speed_ = 0;
  float clock_ = 0;
};

}  // namespace loomia
