// Matemática mínima para o cliente (sem dependências externas).
//
// Vec2/Vec3/Mat4 em coluna (como OpenGL espera), com as operações que o
// renderer e a câmera precisam. Coluna-major: m[coluna * 4 + linha].
#pragma once

#include <cmath>
#include <cstdint>

namespace loomia {

struct Vec2 {
  float x = 0, y = 0;
  Vec2() = default;
  Vec2(float x_, float y_) : x(x_), y(y_) {}
};

struct Vec3 {
  float x = 0, y = 0, z = 0;
  Vec3() = default;
  Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

  Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
  Vec3& operator-=(const Vec3& o) { x -= o.x; y -= o.y; z -= o.z; return *this; }
};

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }

inline Vec3 normalize(const Vec3& v) {
  const float len = length(v);
  return len > 1e-6f ? v * (1.0f / len) : Vec3{0, 0, 0};
}

inline float clampf(float value, float lo, float hi) {
  return value < lo ? lo : (value > hi ? hi : value);
}

inline float radians(float degrees) { return degrees * 3.14159265358979323846f / 180.0f; }

/** Matriz 4x4 coluna-major (formato de `glUniformMatrix4fv`). */
struct Mat4 {
  float m[16] = {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1};

  static Mat4 identity() { return {}; }

  static Mat4 perspective(float fovYRadians, float aspect, float near, float far) {
    Mat4 out;
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    for (float& value : out.m) value = 0;
    out.m[0] = f / (aspect > 0.0001f ? aspect : 1.0f);
    out.m[5] = f;
    out.m[10] = (far + near) / (near - far);
    out.m[11] = -1.0f;
    out.m[14] = (2.0f * far * near) / (near - far);
    return out;
  }

  static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 out;
    out.m[0] = s.x;  out.m[4] = s.y;  out.m[8] = s.z;   out.m[12] = -dot(s, eye);
    out.m[1] = u.x;  out.m[5] = u.y;  out.m[9] = u.z;   out.m[13] = -dot(u, eye);
    out.m[2] = -f.x; out.m[6] = -f.y; out.m[10] = -f.z; out.m[14] = dot(f, eye);
    out.m[3] = 0;    out.m[7] = 0;    out.m[11] = 0;    out.m[15] = 1;
    return out;
  }

  static Mat4 translation(const Vec3& t) {
    Mat4 out;
    out.m[12] = t.x;
    out.m[13] = t.y;
    out.m[14] = t.z;
    return out;
  }

  static Mat4 rotationY(float angle) {
    Mat4 out;
    const float c = std::cos(angle), s = std::sin(angle);
    // Convenção da física (`yaw = atan2(x, z)`): +Z é "frente", e yaw = +90°
    // aponta para +X. Por isso as colunas são (c, 0, s) e (-s, 0, c).
    out.m[0] = c;  out.m[2] = -s;
    out.m[8] = s;  out.m[10] = c;
    return out;
  }

  static Mat4 rotationX(float angle) {
    Mat4 out;
    const float c = std::cos(angle), s = std::sin(angle);
    out.m[5] = c;  out.m[6] = s;
    out.m[9] = -s; out.m[10] = c;
    return out;
  }

  static Mat4 rotationZ(float angle) {
    Mat4 out;
    const float c = std::cos(angle), s = std::sin(angle);
    out.m[0] = c;  out.m[1] = s;
    out.m[4] = -s; out.m[5] = c;
    return out;
  }

  static Mat4 scale(const Vec3& s) {
    Mat4 out;
    out.m[0] = s.x;
    out.m[5] = s.y;
    out.m[10] = s.z;
    return out;
  }

  Mat4 operator*(const Mat4& o) const {
    Mat4 out;
    for (int col = 0; col < 4; col++) {
      for (int row = 0; row < 4; row++) {
        float sum = 0;
        for (int k = 0; k < 4; k++) sum += m[k * 4 + row] * o.m[col * 4 + k];
        out.m[col * 4 + row] = sum;
      }
    }
    return out;
  }

  Mat4& operator*=(const Mat4& o) { *this = *this * o; return *this; }
};

/** Cor RGB 0..255 (formato do `.loom`). */
struct Color3 {
  uint8_t r = 255, g = 255, b = 255;
  float rf() const { return r / 255.0f; }
  float gf() const { return g / 255.0f; }
  float bf() const { return b / 255.0f; }
};

}  // namespace loomia
