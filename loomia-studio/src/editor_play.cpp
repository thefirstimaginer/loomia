// Janela de teste (Play): roda o mapa atual numa janela separada, com a
// MESMA engine do jogo (câmera livre de observador). Fechou, volta ao editor
// com tudo preservado — o editor nem recarrega o mundo.
#include "editor.h"

#include <glad/glad.h>
#include <SFML/Window.hpp>

#include <cstdio>
#include <cmath>

namespace loomia {

namespace {
struct PlayWindow {
  sf::RenderWindow* window = nullptr;
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

  // OpenGL 3.3 core via SFML::Window (sem GLFW).
  sf::ContextSettings settings;
  settings.majorVersion = 3;
  settings.minorVersion = 3;
  settings.attributeFlags = sf::ContextSettings::Core;

  play.window = new sf::RenderWindow(sf::VideoMode(1024, 640),
                                     "Loomia — teste do mapa (feche para voltar)",
                                     sf::Style::Titlebar | sf::Style::Close, settings);
  if (!play.window) {
    setStatus("Play: falha ao criar a janela de teste");
    return;
  }
  play.window->setActive(true);
  play.window->setVerticalSyncEnabled(true);

  std::string error;
  if (!play.renderer.init(error)) {
    setStatus("Play: " + error);
    delete play.window;
    play.window = nullptr;
    return;
  }
  play.renderer.upload(play.world);
  play.ok = true;
  setStatus("Modo teste aberto — feche a janela para voltar ao editor.");

  double lastTime = nowSeconds();
  while (play.window->isOpen()) {
    const double now = nowSeconds();
    const float dt = static_cast<float>(now - lastTime);
    lastTime = now;

    // Eventos SFML.
    sf::Event event;
    while (play.window->pollEvent(event)) {
      if (event.type == sf::Event::Closed) play.window->close();
    }
    if (!play.window->isOpen()) break;

    // Órbita livre: WASD move o alvo, setas giram.
    const float speed = 14.f * dt;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W)) play.target.z -= speed;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S)) play.target.z += speed;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A)) play.target.x -= speed;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D)) play.target.x += speed;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left)) play.yaw -= dt * 1.5f;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right)) play.yaw += dt * 1.5f;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up)) play.pitch = std::min(1.4f, play.pitch + dt);
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down)) play.pitch = std::max(-0.4f, play.pitch - dt);
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Escape)) play.window->close();

    sf::Vector2u size = play.window->getSize();
    int width = static_cast<int>(size.x);
    int height = static_cast<int>(size.y);
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
    play.window->display();
  }

  play.renderer.destroy();
  delete play.window;
  play.window = nullptr;
  play.ok = false;
  // Reassocia o contexto do editor (o teste destruiu o dele).
  window_->setActive(true);
  setStatus("De volta ao editor — alterações preservadas.");
}

}  // namespace loomia
