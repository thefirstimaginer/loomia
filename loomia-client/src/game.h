// Cena do jogo: janela GLFW + render OpenGL do mapa, física local e rede UDP.
//
// O mapa é baixado SÓ para esta sessão (memória): ao sair, nada fica em disco.
#pragma once

#include <string>

#include "avatar.h"
#include "net.h"
#include "physics.h"
#include "profile.h"
#include "renderer.h"
#include "world.h"

struct GLFWwindow;

namespace loomia {

struct PlaySession {
  World world;
  std::string mapName;
  std::string server;
  std::string token;
  std::string appearanceJson;
  bool valid = false;
};

enum class GameResult { Quit, BackToLauncher };

class GameScene {
 public:
  GameScene(Profile& profile);
  ~GameScene();

  /** Abre a janela e joga. Bloqueia até o jogador sair (Esc). */
  GameResult run(const PlaySession& session);

 private:
  bool initWindow(std::string& error);
  void shutdown();

  Profile& profile_;
  GLFWwindow* window_ = nullptr;
  WorldRenderer renderer_;
  GameChannel channel_;
  std::string error_;
};

}  // namespace loomia
