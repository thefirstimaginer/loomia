// Janela de teste (Play): roda o mapa atual numa janela separada, com a
// MESMA engine do jogo (câmera livre de observador). Fechou, volta ao editor
// com tudo preservado — o editor nem recarrega o mundo.
#include "editor.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cmath>

namespace loomia {

namespace {
struct PlayWindow {
  GLFWwindow* window = nullptr;
  World world;
  WorldRenderer renderer;
  float yaw = 0.8f, pitch = 0.5f, dist = 30.f;
  Vec3 target{0, 2, 0};
  bool ok = false;
};
}  // namespace

/** Abre a janela de teste; bloqueia até fechar. O editor fica intacto. */
void EditorApp::runPlayTest() {
  // Copia o estado atual (edição em andamento não é perdida).
  PlayWindow play;
  play.world = world_;
  play.world.name = "[TESTE] " + play.world.name;

  if (!glfwInit()) {
    setStatus("Play: não consegui iniciar o GLFW");
    return;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  play.window = glfwCreateWindow(1024, 640, "Loomia — teste do mapa (feche para voltar)", nullptr, nullptr);
  if (!play.window) {
    setStatus("Play: falha ao criar a janela de teste");
    glfwTerminate();
    return;
  }
  glfwMakeContextCurrent(play.window);
  glfwSwapInterval(1);
  std::string error;
  if (!play.renderer.init(error)) {
    setStatus("Play: " + error);
    glfwDestroyWindow(play.window);
    glfwTerminate();
    return;
  }
  play.renderer.upload(play.world);
  play.ok = true;
  setStatus("Modo teste aberto — feche a janela para voltar ao editor.");

  double lastTime = glfwGetTime();
  while (!glfwWindowShouldClose(play.window)) {
    const double now = glfwGetTime();
    const float dt = static_cast<float>(now - lastTime);
    lastTime = now;
    glfwPollEvents();

    // Órbita livre: WASD move o alvo, setas giram, scroll aproxima.
    const float speed = 14.f * dt;
    if (glfwGetKey(play.window, GLFW_KEY_W) == GLFW_PRESS) play.target.z -= speed;
    if (glfwGetKey(play.window, GLFW_KEY_S) == GLFW_PRESS) play.target.z += speed;
    if (glfwGetKey(play.window, GLFW_KEY_A) == GLFW_PRESS) play.target.x -= speed;
    if (glfwGetKey(play.window, GLFW_KEY_D) == GLFW_PRESS) play.target.x += speed;
    if (glfwGetKey(play.window, GLFW_KEY_LEFT) == GLFW_PRESS) play.yaw -= dt * 1.5f;
    if (glfwGetKey(play.window, GLFW_KEY_RIGHT) == GLFW_PRESS) play.yaw += dt * 1.5f;
    if (glfwGetKey(play.window, GLFW_KEY_UP) == GLFW_PRESS) play.pitch = std::min(1.4f, play.pitch + dt);
    if (glfwGetKey(play.window, GLFW_KEY_DOWN) == GLFW_PRESS) play.pitch = std::max(-0.4f, play.pitch - dt);
    if (glfwGetKey(play.window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(play.window, GLFW_TRUE);

    int width = 1024, height = 640;
    glfwGetFramebufferSize(play.window, &width, &height);
    glViewport(0, 0, width, height);
    const Color3 sky = play.world.sky;
    glClearColor(sky.r / 255.f, sky.g / 255.f, sky.b / 255.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const Vec3 eye{
        play.target.x + play.dist * std::cos(play.pitch) * std::sin(play.yaw),
        play.target.y + play.dist * std::sin(play.pitch),
        play.target.z + play.dist * std::cos(play.pitch) * std::cos(play.yaw)};
    Camera camera;
    camera.position = eye;
    camera.target = play.target;
    play.renderer.draw(camera, static_cast<float>(width) / static_cast<float>(height > 0 ? height : 1));
    glfwSwapBuffers(play.window);
  }

  play.renderer.destroy();
  glfwDestroyWindow(play.window);
  glfwTerminate();
  play.ok = false;
  // Reassocia o contexto do editor (o teste destruiu o dele).
  glfwInit();
  glfwMakeContextCurrent(window_);
  setStatus("De volta ao editor — alterações preservadas.");
}

}  // namespace loomia
