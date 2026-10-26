#include "renderer.h"

#include <glad/glad.h>

#include <cstddef>

namespace loomia {

// ---------------------------------------------------------------------------
// Shaders (GLSL 3.30 core — compatível com OpenGL 3.3+, inclusive Intel HD)
// ---------------------------------------------------------------------------

static const char* WORLD_VERTEX = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;

uniform mat4 uViewProjection;
uniform vec3 uLightDirection;

out vec3 vColor;
out float vLight;

void main() {
  gl_Position = uViewProjection * vec4(aPosition, 1.0);
  float diffuse = max(dot(normalize(aNormal), normalize(-uLightDirection)), 0.0);
  // Ambiente forte: blocos lidos em monitores antigos ficam mais nítidos.
  vLight = 0.55 + 0.45 * diffuse;
  vColor = aColor;
}
)GLSL";

static const char* WORLD_FRAGMENT = R"GLSL(
#version 330 core
in vec3 vColor;
in float vLight;
out vec4 fragColor;
void main() {
  fragColor = vec4(vColor * vLight, 1.0);
}
)GLSL";

static const char* BOX_VERTEX = R"GLSL(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

uniform mat4 uViewProjection;
uniform mat4 uModel;
uniform vec3 uLightDirection;

out vec3 vNormal;
out float vLight;

void main() {
  vec4 world = uModel * vec4(aPosition, 1.0);
  gl_Position = uViewProjection * world;
  vNormal = mat3(uModel) * aNormal;
  float diffuse = max(dot(normalize(vNormal), normalize(-uLightDirection)), 0.0);
  vLight = 0.6 + 0.4 * diffuse;
}
)GLSL";

static const char* BOX_FRAGMENT = R"GLSL(
#version 330 core
in float vLight;
uniform vec3 uColor;
uniform float uEmissive;
out vec4 fragColor;
void main() {
  vec3 color = uColor;
  fragColor = vec4(mix(color * vLight, color, uEmissive), 1.0);
}
)GLSL";

// ---------------------------------------------------------------------------
// ShaderProgram
// ---------------------------------------------------------------------------

static unsigned int compileStage(unsigned int type, const char* source, std::string& error) {
  const unsigned int shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  int ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048] = {0};
    glGetShaderInfoLog(shader, sizeof(log) - 1, nullptr, log);
    error = log;
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

bool ShaderProgram::compile(const char* vertexSource, const char* fragmentSource, std::string& error) {
  destroy();
  const unsigned int vertex = compileStage(GL_VERTEX_SHADER, vertexSource, error);
  if (!vertex) return false;
  const unsigned int fragment = compileStage(GL_FRAGMENT_SHADER, fragmentSource, error);
  if (!fragment) {
    glDeleteShader(vertex);
    return false;
  }
  program_ = glCreateProgram();
  glAttachShader(program_, vertex);
  glAttachShader(program_, fragment);
  glLinkProgram(program_);
  int ok = 0;
  glGetProgramiv(program_, GL_LINK_STATUS, &ok);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  if (!ok) {
    char log[2048] = {0};
    glGetProgramInfoLog(program_, sizeof(log) - 1, nullptr, log);
    error = log;
    destroy();
    return false;
  }
  return true;
}

void ShaderProgram::use() const { glUseProgram(program_); }

void ShaderProgram::destroy() {
  if (program_) {
    glDeleteProgram(program_);
    program_ = 0;
  }
}

int ShaderProgram::uniform(const char* name) const { return glGetUniformLocation(program_, name); }

ShaderProgram::~ShaderProgram() { destroy(); }

// ---------------------------------------------------------------------------
// Camera
// ---------------------------------------------------------------------------

Mat4 Camera::view() const { return Mat4::lookAt(position, target, Vec3{0, 1, 0}); }

Mat4 Camera::projection(float aspect) const {
  return Mat4::perspective(radians(fovDegrees), aspect, nearPlane, farPlane);
}

// ---------------------------------------------------------------------------
// WorldRenderer
// ---------------------------------------------------------------------------

namespace {

struct Vertex {
  float x, y, z;
  float nx, ny, nz;
  float r, g, b;
};

/** Adiciona uma face (4 vértices + 2 triângulos) já com normal. */
void addFace(std::vector<Vertex>& vertices, std::vector<uint32_t>& indices,
             const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
             const Vec3& normal, const Color3& color, float shade) {
  const uint32_t base = static_cast<uint32_t>(vertices.size());
  const Vertex templateVertex{0, 0, 0, normal.x, normal.y, normal.z,
                              color.rf() * shade, color.gf() * shade, color.bf() * shade};
  for (const Vec3& corner : {a, b, c, d}) {
    Vertex vertex = templateVertex;
    vertex.x = corner.x;
    vertex.y = corner.y;
    vertex.z = corner.z;
    vertices.push_back(vertex);
  }
  indices.insert(indices.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
}

}  // namespace

bool WorldRenderer::init(std::string& error) {
  if (!gladLoadGL()) {
    error = "não foi possível carregar as funções OpenGL (a GPU suporta 3.3?)";
    return false;
  }
  if (!worldShader_.compile(WORLD_VERTEX, WORLD_FRAGMENT, error)) return false;
  if (!boxShader_.compile(BOX_VERTEX, BOX_FRAGMENT, error)) return false;

  // ---- malha do mundo --------------------------------------------------
  glGenVertexArrays(1, &worldVao_);
  glGenBuffers(1, &worldVbo_);
  glGenBuffers(1, &worldIbo_);

  glBindVertexArray(worldVao_);
  glBindBuffer(GL_ARRAY_BUFFER, worldVbo_);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, worldIbo_);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, nx)));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, r)));

  // ---- cubo unitário (avatares/itens) ----------------------------------
  glGenVertexArrays(1, &boxVao_);
  glGenBuffers(1, &boxVbo_);
  glGenBuffers(1, &boxIbo_);
  std::vector<Vertex> boxVertices;
  std::vector<uint32_t> boxIndices;
  const Vec3 h{0.5f, 0.5f, 0.5f};
  const Color3 white{255, 255, 255};
  addFace(boxVertices, boxIndices, {-h.x, -h.y, h.z}, {h.x, -h.y, h.z}, {h.x, h.y, h.z}, {-h.x, h.y, h.z},
          {0, 0, 1}, white, 1.0f);
  addFace(boxVertices, boxIndices, {h.x, -h.y, -h.z}, {-h.x, -h.y, -h.z}, {-h.x, h.y, -h.z}, {h.x, h.y, -h.z},
          {0, 0, -1}, white, 0.85f);
  addFace(boxVertices, boxIndices, {-h.x, -h.y, -h.z}, {-h.x, -h.y, h.z}, {-h.x, h.y, h.z}, {-h.x, h.y, -h.z},
          {-1, 0, 0}, white, 0.9f);
  addFace(boxVertices, boxIndices, {h.x, -h.y, h.z}, {h.x, -h.y, -h.z}, {h.x, h.y, -h.z}, {h.x, h.y, h.z},
          {1, 0, 0}, white, 0.75f);
  addFace(boxVertices, boxIndices, {-h.x, h.y, h.z}, {h.x, h.y, h.z}, {h.x, h.y, -h.z}, {-h.x, h.y, -h.z},
          {0, 1, 0}, white, 1.0f);
  addFace(boxVertices, boxIndices, {-h.x, -h.y, -h.z}, {h.x, -h.y, -h.z}, {h.x, -h.y, h.z}, {-h.x, -h.y, h.z},
          {0, -1, 0}, white, 0.55f);

  glBindVertexArray(boxVao_);
  glBindBuffer(GL_ARRAY_BUFFER, boxVbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(boxVertices.size() * sizeof(Vertex)),
               boxVertices.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, boxIbo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,
               static_cast<GLsizeiptr>(boxIndices.size() * sizeof(uint32_t)), boxIndices.data(),
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, x)));
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, nx)));
  glBindVertexArray(0);
  boxIndexCount_ = boxIndices.size();

  ready_ = true;
  return true;
}

void WorldRenderer::destroy() {
  // Shaders também: se ficarem para os destrutores, rodariam depois do
  // glfwTerminate (contexto morto) e davam segfault.
  worldShader_.destroy();
  boxShader_.destroy();
  if (worldIbo_) glDeleteBuffers(1, &worldIbo_);
  if (boxIbo_) glDeleteBuffers(1, &boxIbo_);
  if (worldVbo_) glDeleteBuffers(1, &worldVbo_);
  if (boxVbo_) glDeleteBuffers(1, &boxVbo_);
  if (worldVao_) glDeleteVertexArrays(1, &worldVao_);
  if (boxVao_) glDeleteVertexArrays(1, &boxVao_);
  worldIbo_ = worldVbo_ = worldVao_ = boxVbo_ = boxVao_ = boxIbo_ = 0;
  ready_ = false;
}

void WorldRenderer::upload(const World& world) {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  // Estimativa: 24 vértices por bloco (6 faces x 4).
  vertices.reserve(world.blocks.size() * 24);
  indices.reserve(world.blocks.size() * 36);

  for (const Block& block : world.blocks) {
    const Vec3 h{block.size.x * 0.5f, block.size.y * 0.5f, block.size.z * 0.5f};
    const Vec3& p = block.position;
    // Tons por material dão relevo mesmo com luz uniforme.
    float shade = 1.0f;
    switch (block.material) {
      case Material::Grass: shade = 0.95f; break;
      case Material::Wood: shade = 1.0f; break;
      case Material::Brick: shade = 0.92f; break;
      case Material::Stone: shade = 0.9f; break;
      case Material::Neon: shade = 1.15f; break;
      default: break;
    }

    addFace(vertices, indices, {p.x - h.x, p.y - h.y, p.z + h.z}, {p.x + h.x, p.y - h.y, p.z + h.z},
            {p.x + h.x, p.y + h.y, p.z + h.z}, {p.x - h.x, p.y + h.y, p.z + h.z}, {0, 0, 1}, block.color, shade);
    addFace(vertices, indices, {p.x + h.x, p.y - h.y, p.z - h.z}, {p.x - h.x, p.y - h.y, p.z - h.z},
            {p.x - h.x, p.y + h.y, p.z - h.z}, {p.x + h.x, p.y + h.y, p.z - h.z}, {0, 0, -1}, block.color,
            shade * 0.85f);
    addFace(vertices, indices, {p.x - h.x, p.y - h.y, p.z - h.z}, {p.x - h.x, p.y - h.y, p.z + h.z},
            {p.x - h.x, p.y + h.y, p.z + h.z}, {p.x - h.x, p.y + h.y, p.z - h.z}, {-1, 0, 0}, block.color,
            shade * 0.9f);
    addFace(vertices, indices, {p.x + h.x, p.y - h.y, p.z + h.z}, {p.x + h.x, p.y - h.y, p.z - h.z},
            {p.x + h.x, p.y + h.y, p.z - h.z}, {p.x + h.x, p.y + h.y, p.z + h.z}, {1, 0, 0}, block.color,
            shade * 0.75f);
    addFace(vertices, indices, {p.x - h.x, p.y + h.y, p.z + h.z}, {p.x + h.x, p.y + h.y, p.z + h.z},
            {p.x + h.x, p.y + h.y, p.z - h.z}, {p.x - h.x, p.y + h.y, p.z - h.z}, {0, 1, 0}, block.color,
            shade);
    addFace(vertices, indices, {p.x - h.x, p.y - h.y, p.z - h.z}, {p.x + h.x, p.y - h.y, p.z - h.z},
            {p.x + h.x, p.y - h.y, p.z + h.z}, {p.x - h.x, p.y - h.y, p.z + h.z}, {0, -1, 0}, block.color,
            shade * 0.55f);
  }

  glBindVertexArray(worldVao_);
  glBindBuffer(GL_ARRAY_BUFFER, worldVbo_);
  glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
               vertices.empty() ? nullptr : vertices.data(), GL_STATIC_DRAW);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, worldIbo_);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(uint32_t)),
               indices.empty() ? nullptr : indices.data(), GL_STATIC_DRAW);
  glBindVertexArray(0);

  indexCount_ = indices.size();
  triangles_ = indices.size() / 3;
  vertexCount_ = vertices.size();
}

void WorldRenderer::draw(const Camera& camera, float aspect, const Vec3& lightDir) {
  if (!ready_ || indexCount_ == 0) return;
  worldShader_.use();
  const Mat4 viewProjection = camera.projection(aspect) * camera.view();
  glUniformMatrix4fv(worldShader_.uniform("uViewProjection"), 1, GL_FALSE, viewProjection.m);
  glUniform3f(worldShader_.uniform("uLightDirection"), lightDir.x, lightDir.y, lightDir.z);
  glBindVertexArray(worldVao_);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indexCount_), GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

void WorldRenderer::drawBox(const Camera& camera, float aspect, const Mat4& transform, const Color3& color,
                            bool emissive) {
  if (!ready_) return;
  boxShader_.use();
  const Mat4 viewProjection = camera.projection(aspect) * camera.view();
  glUniformMatrix4fv(boxShader_.uniform("uViewProjection"), 1, GL_FALSE, viewProjection.m);
  glUniformMatrix4fv(boxShader_.uniform("uModel"), 1, GL_FALSE, transform.m);
  glUniform3f(boxShader_.uniform("uLightDirection"), -0.45f, -1.0f, -0.3f);
  glUniform3f(boxShader_.uniform("uColor"), color.rf(), color.gf(), color.bf());
  glUniform1f(boxShader_.uniform("uEmissive"), emissive ? 0.75f : 0.0f);
  glBindVertexArray(boxVao_);
  glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(boxIndexCount_), GL_UNSIGNED_INT, nullptr);
  glBindVertexArray(0);
}

}  // namespace loomia
