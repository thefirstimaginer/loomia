// Renderer OpenGL 3.3 core para o mundo de blocos.
//
// Estratégia: TODOS os blocos viram UMA malha só (um VBO/IBO), com cor por
// vértice. Um shader simples com luz direcional + ambient. Assim o mapa inteiro
// desenha em UM draw call, o que roda bem até em GPUs antigas.
//
// A malha é construída uma vez ao entrar no mapa; só os avatares mudam por
// frame (poucas caixas).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "math.h"
#include "world.h"

namespace loomia {

/** Programa GLSL compilado a partir de duas fontes. */
class ShaderProgram {
 public:
  ShaderProgram() = default;
  ~ShaderProgram();
  ShaderProgram(const ShaderProgram&) = delete;
  ShaderProgram& operator=(const ShaderProgram&) = delete;

  bool compile(const char* vertexSource, const char* fragmentSource, std::string& error);
  void use() const;
  void destroy();

  int uniform(const char* name) const;
  unsigned int id() const { return program_; }

 private:
  unsigned int program_ = 0;
};

struct Camera {
  Vec3 position{0, 10, 20};
  Vec3 target{0, 0, 0};
  float fovDegrees = 70.0f;
  float nearPlane = 0.1f;
  float farPlane = 1500.0f;

  Mat4 view() const;
  Mat4 projection(float aspect) const;
};

class WorldRenderer {
 public:
  bool init(std::string& error);
  void destroy();

  /** Constrói a malha estática do mundo (chamar ao carregar o mapa). */
  void upload(const World& world);

  /** Desenha o mundo. `lightDir` aponta NA direção da luz. */
  void draw(const Camera& camera, float aspect, const Vec3& lightDir = {-0.45f, -1.0f, -0.3f});

  /** Desenha uma caixa colorida (para avatares/itens). */
  void drawBox(const Camera& camera, float aspect, const Mat4& transform, const Color3& color,
               bool emissive = false);

  size_t triangleCount() const { return triangles_; }
  size_t vertexCount() const { return vertexCount_; }

 private:
  ShaderProgram worldShader_;
  ShaderProgram boxShader_;
  unsigned int worldVao_ = 0;
  unsigned int worldVbo_ = 0;
  unsigned int worldIbo_ = 0;
  unsigned int boxVao_ = 0;
  unsigned int boxVbo_ = 0;
  unsigned int boxIbo_ = 0;
  size_t triangles_ = 0;
  size_t indexCount_ = 0;
  size_t boxIndexCount_ = 0;
  size_t vertexCount_ = 0;
  bool ready_ = false;
};

}  // namespace loomia
