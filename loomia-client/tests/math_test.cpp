// Verifica a composição de matrizes do avatar (rotação + posição das partes).
#include <cmath>
#include <cstdio>

#include "math.h"

using namespace loomia;

static bool near(float a, float b) { return std::fabs(a - b) < 1e-4f; }

int main() {
  int failures = 0;
  auto check = [&](const char* name, bool ok) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", name);
    if (!ok) failures++;
  };

  // 1) rotationY(0) é identidade.
  {
    const Mat4 identity = Mat4::rotationY(0);
    bool ok = true;
    for (int i = 0; i < 16; i++) ok = ok && near(identity.m[i], Mat4::identity().m[i]);
    check("rotationY(0) == identidade", ok);
  }

  // 2) rotationY(90°) leva +Z para +X (convenção yaw = atan2(x, z)).
  {
    const Mat4 rotation = Mat4::rotationY(3.14159265f / 2);
    // Ponto (0,0,1) transformado.
    const float x = rotation.m[0] * 0 + rotation.m[4] * 0 + rotation.m[8] * 1;
    const float z = rotation.m[2] * 0 + rotation.m[6] * 0 + rotation.m[10] * 1;
    check("yaw 90 leva +Z para +X", near(x, 1) && near(z, 0));
  }

  // 3) Composto: posição (10,2,3), yaw 90, parte em (0,1,0) com tamanho (1,1,1).
  {
    const Mat4 base = Mat4::translation(Vec3{10, 2, 3}) * Mat4::rotationY(3.14159265f / 2);
    const Mat4 model = base * Mat4::translation(Vec3{0, 1, 0}) * Mat4::scale(Vec3{1, 1, 1});
    // A parte deve estar em x = 10 + 0*... , com o centro em (10, 3, 3).
    const float px = model.m[12];
    const float py = model.m[13];
    const float pz = model.m[14];
    check("parte acompanha a posição do avatar", near(px, 10) && near(py, 3) && near(pz, 3));
    // O 1º vértice do cubo unitário (-0.5,-0.5,-0.5) deve virar (10±0.5, ...).
    const float vx = model.m[0] * -0.5f + model.m[4] * -0.5f + model.m[8] * -0.5f + model.m[12];
    const float vy = model.m[1] * -0.5f + model.m[5] * -0.5f + model.m[9] * -0.5f + model.m[13];
    check("vértice do cubo no lugar", near(vy, 2.5f) && std::fabs(vx - 10) <= 0.51f);
  }

  // 4) Escala aplicada antes do translate infla o deslocamento (regressão).
  {
    const Mat4 wrong = Mat4::translation(Vec3{10, 0, 0}) * Mat4::scale(Vec3{2, 2, 2}) * Mat4::translation(Vec3{0, 1, 0});
    check("escala antes do translate move o centro", near(wrong.m[13], 2));
  }

  std::printf("\n%d falha(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
