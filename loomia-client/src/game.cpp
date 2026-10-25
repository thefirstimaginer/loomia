#include "game.h"

#include "labels.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

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
  if (!glfwInit()) {
    error = "não consegui iniciar o GLFW";
    return false;
  }
  // OpenGL 3.3 core: roda em GPUs de 2012+ (inclusive Intel HD integrada).
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);  // macOS
  glfwWindowHint(GLFW_SAMPLES, 0);
  glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);

  window_ = glfwCreateWindow(1280, 720, "Loomia", nullptr, nullptr);
  if (!window_) {
    error = "não consegui criar a janela OpenGL 3.3 (a GPU suporta?)";
    glfwTerminate();
    return false;
  }
  glfwMakeContextCurrent(window_);
  glfwSwapInterval(1);

  if (!renderer_.init(error)) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
    glfwTerminate();
    return false;
  }
  return true;
}

void GameScene::shutdown() {
  channel_.close();
  renderer_.destroy();
  if (window_) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
  glfwTerminate();
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

  std::string channelError;
  if (!channel_.connect(host, port, session.token, profile_.displayName(), session.appearanceJson,
                        channelError)) {
    std::fprintf(stderr, "[loomia] %s\n", channelError.c_str());
    return GameResult::BackToLauncher;
  }

  // Espera o welcome (a sala valida o token). Até 6 s.
  const double connectedAt = nowSeconds();
  while (!channel_.hasWelcome() && nowSeconds() - connectedAt < 6.0) {
    channel_.poll();
    glfwPollEvents();
    if (glfwWindowShouldClose(window_)) return GameResult::BackToLauncher;
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

  std::printf("[loomia] no mundo \"%s\" (sala %s)\n", session.mapName.c_str(), session.server.c_str());

  // ---- loop ------------------------------------------------------------
  while (!glfwWindowShouldClose(window_)) {
    const double now = nowSeconds();
    float dt = static_cast<float>(now - lastTime);
    lastTime = now;
    dt = std::min(dt, 0.1f);

    // Inputs.
    if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
    static bool vHeld = false;
    if (glfwGetKey(window_, GLFW_KEY_V) == GLFW_PRESS) {
      if (!vHeld) firstPerson = !firstPerson;
      vHeld = true;
    } else {
      vHeld = false;
    }

    const bool forward = glfwGetKey(window_, GLFW_KEY_W) == GLFW_PRESS ||
                         glfwGetKey(window_, GLFW_KEY_UP) == GLFW_PRESS;
    const bool back = glfwGetKey(window_, GLFW_KEY_S) == GLFW_PRESS ||
                      glfwGetKey(window_, GLFW_KEY_DOWN) == GLFW_PRESS;
    const bool left = glfwGetKey(window_, GLFW_KEY_A) == GLFW_PRESS ||
                      glfwGetKey(window_, GLFW_KEY_LEFT) == GLFW_PRESS;
    const bool right = glfwGetKey(window_, GLFW_KEY_D) == GLFW_PRESS ||
                       glfwGetKey(window_, GLFW_KEY_RIGHT) == GLFW_PRESS;
    const bool jump = glfwGetKey(window_, GLFW_KEY_SPACE) == GLFW_PRESS;

    // Gira a câmera com o botão direito (drag).
    double mouseX = 0, mouseY = 0;
    glfwGetCursorPos(window_, &mouseX, &mouseY);
    static double lastMouseX = mouseX, lastMouseY = mouseY;
    if (glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS || firstPerson) {
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
    int width = 0, height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
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
    }

    glfwSwapBuffers(window_);
    glfwPollEvents();
  }

  channel_.close();
  shutdown();
  return GameResult::BackToLauncher;
}

}  // namespace loomia
