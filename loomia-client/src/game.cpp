#include "game.h"

#include "labels.h"

#ifndef LOOMIA_DEBUG_BUILD
#define LOOMIA_DEBUG_BUILD 0
#endif

#include <glad/glad.h>
#include <SFML/Window.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <thread>

namespace loomia {

namespace {

double nowSeconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

struct OtherPlayer {
  Avatar avatar;
  Vec3 position{0, 0, 0};
  float yaw = 0;
  float speed = 0;
};

}  // namespace

GameScene::GameScene(Profile& profile) : profile_(profile) {}

GameScene::~GameScene() { shutdown(); }

bool GameScene::initWindow(std::string& error) {
  // OpenGL 3.3 core: roda em GPUs de 2012+ (inclusive Intel HD integrada).
  sf::ContextSettings settings;
  settings.majorVersion = 3;
  settings.minorVersion = 3;
  settings.attributeFlags = sf::ContextSettings::Core;

  window_ = new sf::RenderWindow(sf::VideoMode(1280, 720), "Loomia",
                                 sf::Style::Titlebar | sf::Style::Close, settings);
  if (!window_) {
    error = "não consegui criar a janela OpenGL 3.3 (a GPU suporta?)";
    return false;
  }
  window_->setActive(true);
  window_->setVerticalSyncEnabled(true);

  // Carrega ponteiros de funções OpenGL via GLAD, usando o loader do SFML.
  if (!gladLoadGLLoader((GLADloadproc)sf::Context::getFunction)) {
    error = "não consegui carregar funções OpenGL (GLAD)";
    return false;
  }

  if (!renderer_.init(error)) {
    delete window_;
    window_ = nullptr;
    return false;
  }
  return true;
}

void GameScene::shutdown() {
  channel_.close();
  renderer_.destroy();
  if (window_) {
    window_->setActive(false);
    delete window_;
    window_ = nullptr;
  }
}

GameResult GameScene::run(const PlaySession& session) {
  std::string error;
  if (!initWindow(error)) {
    std::fprintf(stderr, "[loomia] %s\n", error.c_str());
    error_ = error;
    return GameResult::BackToLauncher;
  }

  // ---- mapa (baixado só para esta sessão) ------------------------------
  std::string host;
  int port = 0;
  if (!parseServerAddress(session.server, host, port)) {
    std::fprintf(stderr, "[loomia] endereço de sala inválido: %s\n", session.server.c_str());
    return GameResult::BackToLauncher;
  }

  renderer_.upload(session.world);
  const std::vector<Collider> colliders = buildColliders(session.world);
  const float minY = worldMinY(session.world);

  // Título com contadores ao vivo (mapa, blocos, vértices, ping).
  const auto updateTitle = [this, &session](float ping) {
    char title[160];
    std::snprintf(title, sizeof(title), "Loomia - %s - Blocos: %zu - Vertices: %zu - Ping: %s",
                  session.mapName.c_str(), session.world.blocks.size(), renderer_.vertexCount(),
                  ping >= 0 ? std::to_string(static_cast<int>(ping)).c_str() : "--");
    window_->setTitle(title);
  };
  updateTitle(-1);

  std::string channelError;
  if (!channel_.connect(host, port, session.token, profile_.displayName(), session.appearanceJson,
                        channelError, profile_.token())) {
    std::fprintf(stderr, "[loomia] %s\n", channelError.c_str());
    return GameResult::BackToLauncher;
  }

  // Espera o welcome (a sala valida o token). Até 6 s.
  const double connectedAt = nowSeconds();
  while (!channel_.hasWelcome() && nowSeconds() - connectedAt < 6.0) {
    channel_.poll();
    sf::Event event;
    while (window_->pollEvent(event)) {
      if (event.type == sf::Event::Closed) return GameResult::BackToLauncher;
    }
    if (!window_->isOpen()) return GameResult::BackToLauncher;
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  if (!channel_.hasWelcome()) {
    std::fprintf(stderr, "[loomia] a sala %s não respondeu (token %s...): %s\n", session.server.c_str(),
                 session.token.substr(0, 6).c_str(),
                 channel_.lastError().empty() ? "sem resposta ao hello" : channel_.lastError().c_str());
    return GameResult::BackToLauncher;
  }

  // ---- estado local ----------------------------------------------------
  CharacterState state;
  state.position = channel_.spawn();
  bool onGround = false;
  (void) onGround;

  Avatar me;
  me.setup(Appearance::fromJson(session.appearanceJson));
  me.setName(profile_.displayName());

  std::map<uint32_t, OtherPlayer> others;

  Camera camera;
  float cameraYaw = 0.0f;
  float cameraPitch = -0.35f;
  float cameraDistance = 11.0f;

  // Etiquetas de nome (texto 2D projetado sobre o mundo).
  LabelRenderer labels;
  std::string labelError;
  const bool labelsReady = labels.init(labelError);
  if (!labelsReady) {
    std::fprintf(stderr, "[loomia] etiquetas desativadas: %s\n", labelError.c_str());
  }

  uint32_t sequence = 0;
  double accumulator = 0;
  double lastTime = nowSeconds();
  double pingTimer = 0;
  uint32_t pingId = 0;
  bool firstPerson = false;
  bool debugHud = LOOMIA_DEBUG_BUILD;

  std::printf("[loomia] no mundo \"%s\" (sala %s)\n", session.mapName.c_str(), session.server.c_str());

  // ---- loop ------------------------------------------------------------
  while (window_->isOpen()) {
    const double now = nowSeconds();
    float dt = static_cast<float>(now - lastTime);
    lastTime = now;
    dt = std::min(dt, 0.1f);

    // Processa eventos da janela (fecha, resize etc.).
    sf::Event event;
    while (window_->pollEvent(event)) {
      if (event.type == sf::Event::Closed) {
        window_->close();
        break;
      }
    }
    if (!window_->isOpen()) break;

    // Inputs.
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Escape)) break;
    static bool vHeld = false;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::V)) {
      if (!vHeld) firstPerson = !firstPerson;
      vHeld = true;
    } else {
      vHeld = false;
    }
    // F3 liga/desliga o HUD de debug (sempre disponível na build debug).
    static bool f3Held = false;
    if (sf::Keyboard::isKeyPressed(sf::Keyboard::Key::F3)) {
      if (!f3Held) debugHud = !debugHud;
      f3Held = true;
    } else {
      f3Held = false;
    }

    const bool forward = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::W) ||
                         sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Up);
    const bool back = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::S) ||
                      sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Down);
    const bool left = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::A) ||
                      sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Left);
    const bool right = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::D) ||
                       sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Right);
    const bool jump = sf::Keyboard::isKeyPressed(sf::Keyboard::Key::Space);

    // Gira a câmera com o botão direito (drag).
    sf::Vector2i mousePos = sf::Mouse::getPosition(*window_);
    double mouseX = static_cast<double>(mousePos.x);
    double mouseY = static_cast<double>(mousePos.y);
    static double lastMouseX = mouseX, lastMouseY = mouseY;
    if (sf::Mouse::isButtonPressed(sf::Mouse::Button::Right) || firstPerson) {
      cameraYaw -= static_cast<float>(mouseX - lastMouseX) * 0.004f;
      cameraPitch -= static_cast<float>(mouseY - lastMouseY) * 0.003f;
      cameraPitch = clampf(cameraPitch, -1.2f, 1.2f);
    }
    lastMouseX = mouseX;
    lastMouseY = mouseY;

    // Movimento relativo à câmera.
    float moveX = 0, moveZ = 0;
    if (forward) moveZ += 1;
    if (back) moveZ -= 1;
    if (right) moveX += 1;
    if (left) moveX -= 1;
    if (moveX != 0 || moveZ != 0) {
      const float forwardX = std::sin(cameraYaw);
      const float forwardZ = std::cos(cameraYaw);
      const float rightX = forwardZ;
      const float rightZ = -forwardX;
      float worldX = forwardX * moveZ + rightX * moveX;
      float worldZ = forwardZ * moveZ + rightZ * moveX;
      const float len = std::sqrt(worldX * worldX + worldZ * worldZ);
      if (len > 0) {
        worldX /= len;
        worldZ /= len;
      }
      moveX = worldX;
      moveZ = worldZ;
    }

    // Previsão local a 30 Hz.
    accumulator += dt;
    while (accumulator >= PhysicsConstants::tickDt) {
      accumulator -= PhysicsConstants::tickDt;
      MoveInput input;
      input.moveX = moveX;
      input.moveZ = moveZ;
      input.jump = jump;
      state = step(state, input, colliders);
      sequence++;
      channel_.sendInput(sequence, moveX, moveZ, jump, state.yaw);
      channel_.pushPending(sequence, moveX, moveZ, jump);
    }

    // Rede.
    channel_.poll();
    channel_.dropAcknowledged(channel_.ack());
    pingTimer += dt;
    if (pingTimer > 1.0) {
      pingTimer = 0;
      channel_.ping(++pingId, nowSeconds());
      updateTitle(channel_.pingMs());
    }

    // Outros jogadores.
    others.clear();
    for (const PlayerSnapshot& snapshot : channel_.players()) {
      OtherPlayer& entry = others[snapshot.idP];
      if (entry.avatar.name().empty()) {
        Appearance look = profile_.appearance();
        if (!snapshot.appearanceJson.empty()) look = Appearance::fromJson(snapshot.appearanceJson);
        if (!snapshot.name.empty()) look.username = snapshot.name;
        entry.avatar.setup(look);
        entry.position = snapshot.position;
      }
      const float dx = snapshot.position.x - entry.position.x;
      const float dz = snapshot.position.z - entry.position.z;
      entry.speed = std::sqrt(dx * dx + dz * dz) / std::max(0.001f, dt);
      entry.position = snapshot.position;
      entry.yaw = snapshot.yaw;
      entry.avatar.update(entry.position, entry.yaw, entry.speed, dt);
    }

    me.update(state.position, state.yaw, std::sqrt(state.velocity.x * state.velocity.x +
                                                   state.velocity.z * state.velocity.z),
              dt);

    // Câmera orbital (ou primeira pessoa).
    const Vec3 focus{state.position.x, state.position.y + PhysicsConstants::eyeHeight, state.position.z};
    if (firstPerson) {
      const float pitch = clampf(cameraPitch, -1.2f, 1.2f);
      camera.position = focus;
      camera.target = focus + Vec3{std::sin(cameraYaw) * std::cos(pitch), -std::sin(pitch),
                                   std::cos(cameraYaw) * std::cos(pitch)};
      camera.fovDegrees = 75;
    } else {
      const float pitch = clampf(cameraPitch, -0.45f, -0.05f);
      const float horizontal = std::cos(pitch) * cameraDistance;
      camera.position = focus + Vec3{-std::sin(cameraYaw) * horizontal, -std::sin(pitch) * cameraDistance,
                                     -std::cos(cameraYaw) * horizontal};
      camera.target = focus;
      camera.fovDegrees = 70;
    }

    // ---- render --------------------------------------------------------
    sf::Vector2u size = window_->getSize();
    int width = static_cast<int>(size.x);
    int height = static_cast<int>(size.y);
    if (width > 0 && height > 0) {
      glViewport(0, 0, width, height);
      const World& world = session.world;
      glClearColor(world.sky.rf(), world.sky.gf(), world.sky.bf(), 1.0f);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glEnable(GL_DEPTH_TEST);
      glEnable(GL_CULL_FACE);
      const float aspect = static_cast<float>(width) / std::max(1, height);

      renderer_.draw(camera, aspect);
      if (!firstPerson) me.draw(renderer_, camera, aspect);
      for (auto& [id, other] : others) {
        (void) id;
        other.avatar.draw(renderer_, camera, aspect);
      }

      // Nomes flutuando acima dos personagens (estilo 2008).
      if (labelsReady) {
        glDisable(GL_CULL_FACE);
        const Mat4 viewProjection = camera.projection(aspect) * camera.view();
        labels.begin(width, height);
        float screenX = 0, screenY = 0;
        auto drawName = [&](const Vec3& feet, const std::string& name, const Color3& color) {
          if (name.empty()) return;
          const Vec3 head{feet.x, feet.y + Avatar::nameHeight, feet.z};
          if (!LabelRenderer::project(viewProjection, head, width, height, screenX, screenY)) return;
          labels.drawCentered(screenX, screenY, name, color, std::max(1.5f, static_cast<float>(height) / 480.f), 1.0f);
        };
        if (!firstPerson) drawName(state.position, me.name(), Color3{255, 255, 255});
        for (auto& [id, other] : others) {
          (void) id;
          drawName(other.position, other.avatar.name(), Color3{232, 238, 251});
        }
        labels.end();
      }

      // HUD de debug (build loomia-client-debug): ping, rede, blocos, coords.
      if (debugHud && labelsReady) {
        const bool online = channel_.hasWelcome() && channel_.isOpen();
        const float ping = channel_.pingMs();
        char line1[96];
        std::snprintf(line1, sizeof(line1), "ping %s  rede %s",
                      ping >= 0 ? std::to_string(static_cast<int>(ping)).c_str() : "--",
                      online ? "ok" : "off");
        char line2[96];
        std::snprintf(line2, sizeof(line2), "blocos %zu  sala %u",
                      session.world.blocks.size(), channel_.localId());
        char line3[96];
        std::snprintf(line3, sizeof(line3), "x %.1f  y %.1f  z %.1f", state.position.x,
                      state.position.y, state.position.z);
        const Color3 hudColor{140, 255, 140};
        labels.begin(width, height);
        const float scale = std::max(1.5f, static_cast<float>(height) / 640.f);
        const float x0 = 12, y0 = 14;
        labels.drawCentered(x0 + 60 * scale, y0, line1, hudColor, scale, 0.9f);
        labels.drawCentered(x0 + 60 * scale, y0 + 18 * scale, line2, hudColor, scale, 0.9f);
        labels.drawCentered(x0 + 60 * scale, y0 + 36 * scale, line3, hudColor, scale, 0.9f);
        labels.end();
      }
    }

    window_->display();
  }

  channel_.close();
  shutdown();
  return GameResult::BackToLauncher;
}

}  // namespace loomia
