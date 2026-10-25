// Rede do cliente: HTTP (master) e UDP (sala).
//
// * HTTP: login, sessão, avatares, catálogo, mapa e "launch". Usa o `curl` do
//   sistema via pipe (sem libcurl), que existe em Linux, macOS e Windows 10+.
// * UDP: protocolo da sala (hello/input/snapshot/chat), idêntico ao
//   `docs/protocolo.md`.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "math.h"

namespace loomia {

struct HttpResponse {
  int status = 0;      // 0 = falha de rede
  std::string body;    // corpo bruto (JSON normalmente)
  std::string error;   // mensagem quando status == 0
  bool ok() const { return status >= 200 && status < 300; }
};

class HttpClient {
 public:
  /** Base do master (ex.: `http://127.0.0.1:8080`). */
  void setOrigin(const std::string& origin);
  const std::string& origin() const { return origin_; }

  /** Cookie de sessão (`loomia_session=...`). */
  void setSessionToken(const std::string& token) { token_ = token; }
  const std::string& sessionToken() const { return token_; }

  HttpResponse get(const std::string& path, int timeoutSeconds = 15);
  HttpResponse post(const std::string& path, const std::string& jsonBody, int timeoutSeconds = 20);
  HttpResponse put(const std::string& path, const std::string& jsonBody);
  HttpResponse del(const std::string& path);

 private:
  HttpResponse request(const char* method, const std::string& path, const std::string* body,
                       int timeoutSeconds);
  std::string origin_;
  std::string token_;
};

/** Arquivo JSON baixado (mapa) ou texto grande, gravado em disco temporário. */
struct DownloadResult {
  bool ok = false;
  std::string path;
  std::string content;
  std::string error;
  int status = 0;
};

/** Baixa uma URL para memória (e opcionalmente para arquivo). */
DownloadResult downloadUrl(const std::string& url, const std::string& sessionToken = "",
                           const std::string& savePath = "");

// ---------------------------------------------------------------------------
// Protocolo da sala (UDP)
// ---------------------------------------------------------------------------

struct PlayerSnapshot {
  uint32_t idP = 0;
  uint32_t ack = 0;
  Vec3 position{0, 0, 0};
  Vec3 velocity{0, 0, 0};
  float yaw = 0;
  bool onGround = false;
  std::string appearanceJson;
  std::string name;
};

struct ChatLine {
  std::string from;
  std::string text;
};

class GameChannel {
 public:
  GameChannel() = default;
  ~GameChannel();

  GameChannel(const GameChannel&) = delete;
  GameChannel& operator=(const GameChannel&) = delete;

  /** Abre o socket UDP e envia `hello`. Devolve false + erro se não abrir. */
  bool connect(const std::string& host, int port, const std::string& token, const std::string& name,
               const std::string& appearanceJson, std::string& error);

  void close();
  bool isOpen() const { return socket_ >= 0; }

  /** Envia um input (30 Hz). */
  void sendInput(uint32_t sequence, float moveX, float moveZ, bool jump, float yaw);
  void sendChat(const std::string& text);
  void sendRespawn();
  void sendBye();
  void ping(uint32_t id, double time);

  /** Processa tudo que chegou (chamar uma vez por frame). */
  void poll();

  // ---- estado das mensagens -------------------------------------------
  bool hasWelcome() const { return hasWelcome_; }
  uint32_t localId() const { return localId_; }
  Vec3 spawn() const { return spawn_; }
  const std::vector<PlayerSnapshot>& players() const { return players_; }
  const std::vector<ChatLine>& pendingChat() { return chat_; }
  void clearChat() { chat_.clear(); }
  float pingMs() const { return pingMs_; }
  uint32_t ack() const { return ack_; }
  const std::string& lastError() const { return lastError_; }

  /** Reconciliação: inputs ainda não confirmados pelo servidor. */
  struct PendingInput {
    uint32_t seq;
    float moveX;
    float moveZ;
    bool jump;
  };
  const std::vector<PendingInput>& pending() const { return pending_; }
  void dropAcknowledged(uint32_t ack);
  void pushPending(uint32_t seq, float moveX, float moveZ, bool jump);

 private:
  void handleMessage(const std::string& text);
  void sendRaw(const std::string& text);

  int socket_ = -1;
  std::string host_;
  int port_ = 0;
  std::string token_;
  std::string helloJson_;
  double lastHello_ = 0;
  bool hasWelcome_ = false;
  uint32_t localId_ = 0;
  Vec3 spawn_{0, 1, 0};
  std::vector<PlayerSnapshot> players_;
  std::vector<ChatLine> chat_;
  std::vector<PendingInput> pending_;
  float pingMs_ = -1;
  uint32_t ack_ = 0;
  std::string lastError_;
};

/** Monta o link `server=host:porta` a partir de uma string obtida no launch. */
bool parseServerAddress(const std::string& server, std::string& host, int& port);

}  // namespace loomia
