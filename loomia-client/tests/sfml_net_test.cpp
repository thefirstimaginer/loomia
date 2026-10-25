// Isola a diferença: mesmo binário, com e sem janela SFML.
//
//   ./loomia-tests-sfml-net host:porta token              (com janela)
//   ./loomia-tests-sfml-net host:porta token --nowindow   (sem janela)
#include <SFML/Graphics.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <thread>

#include "net.h"

int main(int argc, char** argv) {
  if (argc < 3) return 2;
  const bool withWindow = !(argc > 3 && std::strcmp(argv[3], "--nowindow") == 0);

  std::optional<sf::RenderWindow> window;
  if (withWindow) {
    sf::ContextSettings settings;
    settings.majorVersion = 3;
    settings.minorVersion = 3;
    window.emplace(sf::VideoMode({640, 400}), "probe");
    std::printf("janela SFML aberta\n");
  }

  std::string host;
  int port = 0;
  loomia::parseServerAddress(argv[1], host, port);

  loomia::GameChannel channel;
  std::string error;
  const bool ok = channel.connect(host, port, argv[2], "Probe", "{\"username\":\"Probe\"}", error);
  std::printf("connect: %s %s\n", ok ? "ok" : "falhou", error.c_str());

  int polls = 0;
  for (int attempt = 0; attempt < 300 && !channel.hasWelcome(); attempt++) {
    channel.poll();
    polls++;
    if (window) {
      window->clear(sf::Color(10, 10, 10));
      window->display();
      while (const std::optional next = window->pollEvent()) {
        (void) next;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  std::printf("welcome: %s (id=%u, polls=%d)\n", channel.hasWelcome() ? "sim" : "NAO", channel.localId(),
              polls);
  return channel.hasWelcome() ? 0 : 1;
}
