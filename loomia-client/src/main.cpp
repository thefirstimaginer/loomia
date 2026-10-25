// Loomia Client — ponto de entrada.
//
// Fluxo:
//   1. lê o link `loomia://launch?...` (primeiro argumento) ou fica aguardando
//   2. abre o launcher (SFML): conta, perfil e editor de avatar
//   3. quando o jogador clica em "Jogar" no site, o link chega (UDP local ou
//      arquivo) e o cliente baixa o mapa, entra na sala e abre o jogo (OpenGL)
//
// O launcher fica ativo enquanto o jogador navega: o cliente só troca para a
// janela do jogo no momento do play.
#include <SFML/Graphics.hpp>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "game.h"
#include "launcher.h"
#include "master_api.h"
#include "args_parse.h"
#include "net.h"
#include "profile.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <arpa/inet.h>
#include <csignal>
#include <cstdlib>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

constexpr int kBridgePort = 7788;

/** Caminho do arquivo onde outra instância deixa o link pendente. */
std::string linkFilePath() {
#ifdef _WIN32
  const char* temp = std::getenv("TEMP");
  return std::string(temp ? temp : ".") + "\\loomia_launch.txt";
#else
  const char* xdg = std::getenv("XDG_RUNTIME_DIR");
  const char* temp = std::getenv("TMPDIR");
  return std::string(xdg ? xdg : (temp ? temp : "/tmp")) + "/loomia_launch.txt";
#endif
}

/** Envia um link para a instância em execução (UDP local). */
bool notifyRunningInstance(const std::string& link) {
#ifdef _WIN32
  // Windows: escreve o arquivo; a instância principal monitora.
  std::ofstream file(linkFilePath(), std::ios::trunc);
  if (!file) return false;
  file << link;
  return true;
#else
  const int socketHandle = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (socketHandle < 0) return false;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(kBridgePort);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  const ssize_t sent = sendto(socketHandle, link.data(), link.size(), 0,
                              reinterpret_cast<sockaddr*>(&address), sizeof(address));
  ::close(socketHandle);
  return sent > 0;
#endif
}

}  // namespace

int main(int argc, char** argv) {
  std::string link;
  for (int index = 1; index < argc; index++) {
    const std::string argument = argv[index];
    if (argument.rfind("loomia:", 0) == 0) {
      link = argument;
      break;
    }
    if (argument.find("server=") != std::string::npos) {
      // Junta `server=... token=... map=...` num único texto.
      for (int extra = index; extra < argc; extra++) {
        link += std::string(argv[extra]) + " ";
      }
      break;
    }
  }

  // Se houver link e outra instância rodando, entrega e sai.
  if (!link.empty() && notifyRunningInstance(link)) {
    std::printf("[loomia] link entregue ao cliente em execução\n");
    return 0;
  }

  loomia::Profile profile;
  profile.load();
  loomia::HttpClient http;
  http.setOrigin(profile.origin());
  if (profile.loggedIn()) http.setSessionToken("loomia_session=" + profile.token());
  loomia::MasterApi api(http);

  // Janela SFML do launcher.
  sf::ContextSettings settings;
  settings.antiAliasingLevel = 0;
  settings.majorVersion = 3;
  settings.minorVersion = 3;
  settings.attributeFlags = sf::ContextSettings::Default;
  sf::RenderWindow window(sf::VideoMode({1080, 720}), "Loomia",
                          sf::Style::Titlebar | sf::Style::Close, sf::State::Windowed, settings);
  // Vsync desligado: o `display()` com vsync espera o retorno vertical do
  // compositor; em alguns ambientes (compositor pausado, X11 sem DRI) ele
  // trava o loop e o cliente para de ler o link `loomia://` chegando.
  window.setVerticalSyncEnabled(false);
  window.setFramerateLimit(60);

  loomia::Launcher launcher(window, profile, api, http);
  std::string initError;
  if (!launcher.init(initError)) {
    std::fprintf(stderr, "[loomia] %s\n", initError.c_str());
    return 1;
  }
  std::printf("[loomia] launcher pronto (janela %dx%d, OpenGL carregado)\n", window.getSize().x,
              window.getSize().y);
  std::fflush(stdout);

  // Link recebido na linha de comando já entra na fila.
  if (!link.empty()) {
    loomia::LaunchInfo launch = loomia::parseLaunchText(link);
    if (launch.valid) {
      if (!launch.origin.empty()) profile.setOrigin(launch.origin);
      launcher.queueLaunch(launch);
    }
  }
  profile.save();

  // Bridge UDP: recebe links de outras instâncias enquanto o launcher está aberto.
  // Bridge UDP em THREAD PRÓPRIA: com o jogo rodando (game.run bloqueia) ou o
  // SFML segurando o contexto, o `recv` no loop principal nunca era chamado e
  // o link ficava para sempre na fila do socket.
  std::mutex linksMutex;
  std::vector<std::string> incomingLinks;
  std::atomic<bool> bridgeRunning{true};
  std::thread bridgeThread([&] {
    int socketHandle = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socketHandle < 0) return;
    int reuse = 1;
    setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(kBridgePort);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(socketHandle, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
      ::close(socketHandle);
      return;
    }
    std::printf("[loomia] bridge udp na thread (porta %d)\n", kBridgePort);
    std::fflush(stdout);
    while (bridgeRunning.load()) {
      char buffer[2048];
      const ssize_t received = recv(socketHandle, buffer, sizeof(buffer) - 1, 0);
      if (received > 0) {
        std::lock_guard<std::mutex> guard(linksMutex);
        incomingLinks.emplace_back(buffer, static_cast<size_t>(received));
      } else if (received < 0 && errno != EINTR) {
        break;
      }
    }
    ::close(socketHandle);
  });
  bridgeThread.detach();

  std::string lastFileContent;
  loomia::GameScene game(profile);
  bool running = true;

  std::fflush(stdout);
  int frameCount = 0;

  while (running) {
    // ---- eventos SFML ------------------------------------------------
    std::optional<sf::Event> event;
    while (const std::optional next = window.pollEvent()) {
      event = next;
      if (next->is<sf::Event::Closed>()) running = false;
    }
    if (++frameCount % 300 == 0) {
      std::printf("[loomia] pre-frame %d\n", frameCount);
      std::fflush(stdout);
    }
    const loomia::LauncherResult result = launcher.frame(event, 1.0f / 60.0f);
    if (++frameCount % 300 == 0) {
      std::printf("[loomia] frame %d vivo\n", frameCount);
      std::fflush(stdout);
    }
    if (result == loomia::LauncherResult::Quit) {
      std::printf("[loomia] launcher pediu para sair\n");
      running = false;
    }

    // ---- links pendentes (UDP + arquivo) ------------------------------
    // Links entregues pela thread do bridge.
    {
      std::vector<std::string> pending;
      {
        std::lock_guard<std::mutex> guard(linksMutex);
        pending.swap(incomingLinks);
      }
      for (const std::string& incoming : pending) {
        loomia::LaunchInfo launch = loomia::parseLaunchText(incoming);
        if (!launch.valid) continue;
        if (!launch.origin.empty()) profile.setOrigin(launch.origin);
        launcher.queueLaunch(launch);
        std::printf("[loomia] link recebido: mapa %s\n", launch.mapId.c_str());
        std::fflush(stdout);
      }
    }
    {
      std::ifstream file(linkFilePath());
      if (file) {
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (!content.empty() && content != lastFileContent) {
          lastFileContent = content;
          loomia::LaunchInfo launch = loomia::parseLaunchText(content);
          if (launch.valid) {
            if (!launch.origin.empty()) profile.setOrigin(launch.origin);
            launcher.queueLaunch(launch);
            std::printf("[loomia] link recebido do site (arquivo): mapa %s\n", launch.mapId.c_str());
          }
          std::remove(linkFilePath().c_str());
        }
      }
    }

    // ---- jogar -------------------------------------------------------
    loomia::LaunchInfo launch;
    if (launcher.hasPendingLaunch(launch)) {
      http.setOrigin(profile.origin());
      if (profile.loggedIn()) http.setSessionToken("loomia_session=" + profile.token());

      // Se o link veio do site sem servidor (site antigo), aloca a sala.
      if (launch.server.empty() && !launch.mapId.empty()) {
        const loomia::LaunchResult alloc = api.launch(launch.mapId, profile.activeAvatar()
                                                                     ? profile.activeAvatar()->id
                                                                     : std::string());
        if (alloc.ok) {
          launch.server = alloc.server;
          launch.token = alloc.token;
        }
      }

      if (launch.valid) {
        std::string mapJson;
        std::string mapError;
        if (!api.downloadMap(launch.mapId, mapJson, mapError)) {
          std::fprintf(stderr, "[loomia] %s\n", mapError.c_str());
        } else {
          loomia::PlaySession session;
          if (loomia::parseWorld(mapJson, session.world, mapError)) {
            session.mapName = session.world.name;
            session.server = launch.server;
            session.token = launch.token;
            session.appearanceJson = profile.appearance().toJson();
            session.valid = true;

            window.setVisible(false);
            const loomia::GameResult outcome = game.run(session);
            (void) outcome;
            window.setVisible(true);
            window.requestFocus();
          } else {
            std::fprintf(stderr, "[loomia] mapa inválido: %s\n", mapError.c_str());
          }
        }
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if (!window.isOpen()) {
      std::printf("[loomia] janela fechou\n");
      break;
    }
  }

  profile.save();
  std::printf("[loomia] encerrando\n");
  std::fflush(stdout);
#ifdef _WIN32
#else

#endif
  return 0;
}
