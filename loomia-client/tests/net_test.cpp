// Testa o canal UDP do cliente contra uma sala do Go, sem abrir janela.
//
//   ./loomia-tests-udp <host:porta> <token>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#include "net.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::printf("uso: %s host:porta token\n", argv[0]);
    return 2;
  }
  std::string host;
  int port = 0;
  if (!loomia::parseServerAddress(argv[1], host, port)) {
    std::printf("endereço inválido: %s\n", argv[1]);
    return 2;
  }

  loomia::GameChannel channel;
  std::string error;
  if (!channel.connect(host, port, argv[2], "Teste", "{\"username\":\"Teste\"}", error)) {
    std::printf("connect falhou: %s\n", error.c_str());
    return 1;
  }
  std::printf("conectado em %s:%d, aguardando welcome...\n", host.c_str(), port);

  const auto start = std::chrono::steady_clock::now();
  for (int attempt = 0; attempt < 400; attempt++) {
    channel.poll();
    if (channel.hasWelcome()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  const double elapsed =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  if (channel.hasWelcome()) {
    std::printf("welcome em %.2fs: id=%u spawn=(%.1f, %.1f, %.1f) jogadores=%zu\n", elapsed,
                channel.localId(), channel.spawn().x, channel.spawn().y, channel.spawn().z,
                channel.players().size());

    // Envia alguns inputs e confere o ack.
    for (uint32_t seq = 1; seq <= 5; seq++) {
      channel.sendInput(seq, 0, 1, false, 0);
      std::this_thread::sleep_for(std::chrono::milliseconds(40));
      channel.poll();
    }
    std::printf("ack recebido: %u\n", channel.ack());
    channel.close();
    return channel.ack() > 0 ? 0 : 3;
  }

  std::printf("sem welcome após %.2fs (erro: %s)\n", elapsed, channel.lastError().c_str());
  return 1;
}
