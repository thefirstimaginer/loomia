#include "net.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketType = SOCKET;
#define CLOSE_SOCKET closesocket
#define INVALID_SOCK (-1)
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>
using SocketType = int;
#define CLOSE_SOCKET ::close
#define INVALID_SOCK (-1)
#endif

#include "json.h"

namespace loomia {

namespace {

#ifdef _WIN32
struct WinsockInit {
  WinsockInit() {
    WSADATA data;
    WSAStartup(MAKEWORD(2, 2), &data);
  }
  ~WinsockInit() { WSACleanup(); }
};
WinsockInit g_winsock;
#endif

/** Executa um comando e devolve a saída (sem esperar interação). */
std::string runCommand(const std::string& command, int& exitCode) {
  exitCode = -1;
  std::string output;
#ifdef _WIN32
  FILE* pipe = _popen(command.c_str(), "rb");
#else
  FILE* pipe = popen(command.c_str(), "r");
#endif
  if (!pipe) return output;
  std::array<char, 4096> buffer{};
  size_t read = 0;
  while ((read = std::fread(buffer.data(), 1, buffer.size(), pipe)) > 0) {
    output.append(buffer.data(), read);
  }
#ifdef _WIN32
  exitCode = _pclose(pipe);
#else
  exitCode = pclose(pipe);
#endif
  return output;
}

std::string shellQuote(const std::string& text) {
#ifdef _WIN32
  std::string quoted = "\"";
  for (char c : text) {
    if (c == '"') quoted += "\\\"";
    else quoted += c;
  }
  return quoted + "\"";
#else
  std::string quoted = "'";
  for (char c : text) {
    if (c == '\'') quoted += "'\\''";
    else quoted += c;
  }
  return quoted + "'";
#endif
}

bool writeFile(const std::string& path, const std::string& content) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) return false;
  file.write(content.data(), static_cast<std::streamsize>(content.size()));
  return static_cast<bool>(file);
}

std::string readFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) return "";
  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

double nowSeconds() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

}  // namespace

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

void HttpClient::setOrigin(const std::string& origin) {
  origin_ = origin;
  while (!origin_.empty() && origin_.back() == '/') origin_.pop_back();
}

HttpResponse HttpClient::get(const std::string& path, int timeoutSeconds) {
  return request("GET", path, nullptr, timeoutSeconds);
}

HttpResponse HttpClient::post(const std::string& path, const std::string& jsonBody, int timeoutSeconds) {
  return request("POST", path, &jsonBody, timeoutSeconds);
}

HttpResponse HttpClient::put(const std::string& path, const std::string& jsonBody) {
  return request("PUT", path, &jsonBody, 20);
}

HttpResponse HttpClient::del(const std::string& path) { return request("DELETE", path, nullptr, 20); }

HttpResponse HttpClient::request(const char* method, const std::string& path, const std::string* body,
                                int timeoutSeconds) {
  HttpResponse result;
  if (origin_.empty()) {
    result.error = "cliente sem endereço do servidor";
    return result;
  }

  // Corpo e status vão em arquivos separados: no pipe o `-w` se misturava ao
  // corpo em algumas plataformas e o JSON parecia inválido.
#ifdef _WIN32
  const std::string bodyFile = std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "\\loomia_http_body.txt";
#else
  const std::string bodyFile = "/tmp/loomia_http_body.txt";
#endif
  std::remove(bodyFile.c_str());

  // Corpo em arquivo; o STATUS sai no stdout via `-w` (só o número).
  std::ostringstream command;
  command << "curl -sS -m " << timeoutSeconds << " --connect-timeout 5 "
          << "-o " << shellQuote(bodyFile) << " "
          << "-w '%{http_code}' "
          << "-X " << method << " "
          << "-H 'content-type: application/json' ";
  if (!token_.empty()) {
    command << "-H " << shellQuote("cookie: " + token_) << " ";
  }
  if (body) {
    command << "--data-binary " << shellQuote(*body) << " ";
  }
  command << shellQuote(origin_ + path);

  int exitCode = -1;
  const std::string statusText = runCommand(command.str(), exitCode);
  char* end = nullptr;
  const long parsed = std::strtol(statusText.c_str(), &end, 10);
  if (parsed > 0) result.status = static_cast<int>(parsed);

  result.body = readFile(bodyFile);
  std::remove(bodyFile.c_str());

  if (result.status == 0) {
    result.error = "não consegui falar com " + origin_ + " (servidor fora do ar?)";
  }
  return result;
}

DownloadResult downloadUrl(const std::string& url, const std::string& sessionToken,
                           const std::string& savePath) {
  DownloadResult result;
#ifdef _WIN32
  const std::string target = savePath.empty() ? "loomia_download.tmp" : savePath;
  const std::string headers = target + ".headers";
#else
  const std::string target = savePath.empty() ? "/tmp/loomia_download.tmp" : savePath;
  const std::string headers = target + ".headers";
#endif
  std::ostringstream command;
  command << "curl -sS --fail -m 60 --connect-timeout 8 "
          << "-D " << shellQuote(headers) << " "
          << "-o " << shellQuote(target) << " ";
  if (!sessionToken.empty()) command << "-H " << shellQuote("cookie: " + sessionToken) << " ";
  command << shellQuote(url) << " 2>/dev/null; echo -n $?";
  int exitCode = -1;
  const std::string code = runCommand(command.str(), exitCode);

  const std::string headerText = readFile(headers);
  std::remove(headers.c_str());
  size_t lastStatus = headerText.rfind("HTTP/");
  if (lastStatus != std::string::npos) {
    result.status = std::atoi(headerText.substr(lastStatus + 5).c_str());
  }

  result.content = readFile(target);
  if (savePath.empty()) std::remove(target.c_str());

  if (code != "0" || exitCode != 0) {
    result.error = "download falhou (curl saiu com " + code + ") em " + url;
    return result;
  }
  if (result.content.empty()) {
    result.error = "o servidor devolveu um arquivo vazio (" + url + ")";
    return result;
  }
  result.ok = true;
  result.path = savePath;
  return result;
}

// ---------------------------------------------------------------------------
// Canal UDP
// ---------------------------------------------------------------------------

bool parseServerAddress(const std::string& server, std::string& host, int& port) {
  const size_t colon = server.rfind(':');
  if (colon == std::string::npos) return false;
  host = server.substr(0, colon);
  port = std::atoi(server.substr(colon + 1).c_str());
  return !host.empty() && port > 0 && port < 65536;
}

GameChannel::~GameChannel() { close(); }

bool GameChannel::connect(const std::string& host, int port, const std::string& token,
                          const std::string& name, const std::string& appearanceJson,
                          std::string& error) {
  close();
  host_ = host;
  port_ = port;
  token_ = token;

  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  addrinfo* result = nullptr;
  const std::string portText = std::to_string(port);
  if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &result) != 0 || !result) {
    error = "não consegui resolver o endereço da sala: " + host;
    return false;
  }

  SocketType handle = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
  if (handle == INVALID_SOCK) {
    freeaddrinfo(result);
    error = "não consegui criar o socket UDP";
    return false;
  }
  // Não bloqueante: `poll()` nunca trava o frame.
#ifdef _WIN32
  u_long mode = 1;
  ioctlsocket(handle, FIONBIO, &mode);
  int timeout = 3000;
  setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
  // Socket NÃO bloqueante de verdade: com o `SO_RCVTIMEO`, o `recv` sem
  // `O_NONBLOCK` esperava 3 s e engolia os pacotes quando o SFML segurava o
  // contexto OpenGL (o welcome chegava mas era perdido).
  const int flags = fcntl(handle, F_GETFL, 0);
  fcntl(handle, F_SETFL, flags | O_NONBLOCK);
#endif
  // "Conecta" o socket ao destino: assim só recebemos desta sala.
  if (::connect(handle, result->ai_addr, static_cast<int>(result->ai_addrlen)) != 0) {
    freeaddrinfo(result);
    CLOSE_SOCKET(handle);
    error = "a sala " + host + ":" + portText + " está inacessível";
    return false;
  }
  freeaddrinfo(result);
#ifndef _WIN32
  // Depois do `connect`, o timeout de recepção vira EAGAIN; usamos MSG_DONTWAIT
  // no recv para leitura não bloqueante.
#endif
  socket_ = static_cast<int>(handle);

  // hello: repete até o welcome (UDP pode perder o primeiro).
  std::string appearance = appearanceJson.empty() ? "{\"username\":\"" + name + "\"}" : appearanceJson;
  auto hello = json::makeObject();
  json::set(*hello, "t", json::makeString("hello"));
  json::set(*hello, "token", json::makeString(token));
  json::set(*hello, "name", json::makeString(name));
  const json::ValuePtr look = json::parse(appearance);
  json::set(*hello, "appearance", look ? look : json::makeObject());
  helloJson_ = json::writeCompact(*hello);
  sendRaw(helloJson_);
  lastHello_ = nowSeconds();
  hasWelcome_ = false;
  lastError_.clear();
  error.clear();
  return true;
}

void GameChannel::close() {
  if (socket_ >= 0) {
    if (hasWelcome_) sendBye();
    CLOSE_SOCKET(static_cast<SocketType>(socket_));
    socket_ = -1;
  }
  hasWelcome_ = false;
  players_.clear();
  pending_.clear();
}

void GameChannel::sendRaw(const std::string& text) {
  if (socket_ < 0) return;
  send(static_cast<SocketType>(socket_), text.data(), text.size(), 0);
}

void GameChannel::sendInput(uint32_t sequence, float moveX, float moveZ, bool jump, float yaw) {
  auto message = json::makeObject();
  json::set(*message, "t", json::makeString("input"));
  json::set(*message, "seq", json::makeNumber(sequence));
  json::set(*message, "move_x", json::makeNumber(moveX));
  json::set(*message, "move_z", json::makeNumber(moveZ));
  json::set(*message, "jump", json::makeBool(jump));
  json::set(*message, "yaw", json::makeNumber(yaw));
  sendRaw(json::writeCompact(*message));
}

void GameChannel::sendChat(const std::string& text) {
  auto message = json::makeObject();
  json::set(*message, "t", json::makeString("chat"));
  json::set(*message, "text", json::makeString(text));
  sendRaw(json::writeCompact(*message));
}

void GameChannel::sendRespawn() {
  sendRaw("{\"t\":\"respawn\"}");
}

void GameChannel::sendBye() { sendRaw("{\"t\":\"bye\"}"); }

void GameChannel::ping(uint32_t id, double time) {
  auto message = json::makeObject();
  json::set(*message, "t", json::makeString("ping"));
  json::set(*message, "id", json::makeNumber(id));
  json::set(*message, "time", json::makeNumber(time));
  sendRaw(json::writeCompact(*message));
}

void GameChannel::poll() {
  if (socket_ < 0) return;
  // Reenvia o `hello` a cada 500 ms enquanto a sala não confirmar (UDP perde
  // pacotes — era o motivo intermitente de "a sala não respondeu").
  if (!hasWelcome_ && !helloJson_.empty() && nowSeconds() - lastHello_ > 0.5) {
    sendRaw(helloJson_);
    lastHello_ = nowSeconds();
  }
  std::array<char, 65536> buffer{};
  // Lê todos os datagramas disponíveis neste frame.
  for (;;) {
#ifdef _WIN32
    const int received = recv(static_cast<SocketType>(socket_), buffer.data(),
                              static_cast<int>(buffer.size()), 0);
    if (received <= 0) break;
#else
    const ssize_t received = recv(static_cast<SocketType>(socket_), buffer.data(), buffer.size(), MSG_DONTWAIT);
    if (received <= 0) break;
#endif
    handleMessage(std::string(buffer.data(), static_cast<size_t>(received)));
  }
}

void GameChannel::handleMessage(const std::string& text) {
  const json::ValuePtr message = json::parse(text);
  if (!message) return;
  const std::string type = message->str("t");

  if (type == "welcome") {
    hasWelcome_ = true;
    localId_ = static_cast<uint32_t>(message->num("id", 0));
    if (const json::ValuePtr spawn = message->get("spawn")) {
      spawn_ = {static_cast<float>(spawn->at(0)), static_cast<float>(spawn->at(1)),
                static_cast<float>(spawn->at(2))};
    }
    if (const json::ValuePtr players = message->get("players")) {
      for (size_t index = 0; index < players->size(); index++) {
        const json::ValuePtr entry = players->item(index);
        if (!entry) continue;
        PlayerSnapshot snapshot;
        snapshot.idP = static_cast<uint32_t>(entry->num("id", 0));
        snapshot.name = entry->str("name", "Jogador");
        const json::ValuePtr p = entry->get("p");
        if (p) snapshot.position = {static_cast<float>(p->at(0)), static_cast<float>(p->at(1)),
                                    static_cast<float>(p->at(2))};
        snapshot.yaw = static_cast<float>(entry->num("yaw", 0));
        if (const json::ValuePtr look = entry->get("appearance")) {
          snapshot.appearanceJson = json::writeCompact(*look);
        }
        players_.push_back(snapshot);
      }
    }
    return;
  }

  if (type == "snapshot") {
    // Marca quem veio neste snapshot: quem não veio saiu da sala. Sem isso o
    // avatar antigo de um jogador que reconectou (com outro id) ficava
    // congelado no mapa — o "clone" parado no spawn.
    std::vector<uint32_t> seen;
    if (const json::ValuePtr players = message->get("players")) {
      for (size_t index = 0; index < players->size(); index++) {
        const json::ValuePtr entry = players->item(index);
        if (!entry) continue;
        const uint32_t id = static_cast<uint32_t>(entry->num("id", 0));
        seen.push_back(id);
        // O ack do PRÓPRIO jogador vem em players[].ack (regra do protocolo).
        if (id == localId_) {
          ack_ = static_cast<uint32_t>(entry->num("ack", ack_));
          continue;
        }
        PlayerSnapshot snapshot;
        snapshot.idP = id;
        snapshot.name = entry->str("name", "");
        const json::ValuePtr p = entry->get("p");
        if (p) snapshot.position = {static_cast<float>(p->at(0)), static_cast<float>(p->at(1)),
                                    static_cast<float>(p->at(2))};
        const json::ValuePtr v = entry->get("v");
        if (v) snapshot.velocity = {static_cast<float>(v->at(0)), static_cast<float>(v->at(1)),
                                    static_cast<float>(v->at(2))};
        snapshot.yaw = static_cast<float>(entry->num("yaw", 0));
        snapshot.onGround = entry->flag("ground", false);
        if (const json::ValuePtr look = entry->get("appearance")) {
          snapshot.appearanceJson = json::writeCompact(*look);
        }

        bool replaced = false;
        for (PlayerSnapshot& existing : players_) {
          if (existing.idP == id) {
            existing = snapshot;
            replaced = true;
            break;
          }
        }
        if (!replaced) players_.push_back(snapshot);
      }
    }
    // Mantém só quem apareceu neste snapshot (o próprio jogador não está na
    // lista, então ele nunca entra em `players_`).
    for (auto it = players_.begin(); it != players_.end();) {
      bool found = false;
      for (uint32_t id : seen) {
        if (id == it->idP) {
          found = true;
          break;
        }
      }
      it = found ? it + 1 : players_.erase(it);
    }
    return;
  }

  if (type == "players") {
    // Lista de presença: remove quem saiu.
    std::vector<uint32_t> alive;
    if (const json::ValuePtr list = message->get("players")) {
      for (size_t index = 0; index < list->size(); index++) {
        const json::ValuePtr entry = list->item(index);
        if (entry) alive.push_back(static_cast<uint32_t>(entry->num("id", 0)));
      }
    }
    for (auto it = players_.begin(); it != players_.end();) {
      bool found = it->idP == localId_;
      for (uint32_t id : alive) {
        if (id == it->idP) found = true;
      }
      it = found ? it + 1 : players_.erase(it);
    }
    return;
  }

  if (type == "chat") {
    ChatLine line;
    line.from = message->str("from", "?");
    line.text = message->str("text", "");
    chat_.push_back(line);
    if (chat_.size() > 40) chat_.erase(chat_.begin());
    return;
  }

  if (type == "pong") {
    const double sent = message->num("time", 0);
    if (sent > 0) pingMs_ = static_cast<float>((nowSeconds() - sent) * 1000.0);
    return;
  }

  if (type == "respawn") {
    if (const json::ValuePtr p = message->get("p")) {
      spawn_ = {static_cast<float>(p->at(0)), static_cast<float>(p->at(1)), static_cast<float>(p->at(2))};
    }
    return;
  }

  if (type == "error") {
    lastError_ = message->str("message", "erro da sala");
  }
}

void GameChannel::dropAcknowledged(uint32_t ack) {
  std::vector<PendingInput> remaining;
  for (const PendingInput& input : pending_) {
    if (input.seq > ack) remaining.push_back(input);
  }
  pending_ = std::move(remaining);
}

void GameChannel::pushPending(uint32_t seq, float moveX, float moveZ, bool jump) {
  pending_.push_back({seq, moveX, moveZ, jump});
  if (pending_.size() > 120) pending_.erase(pending_.begin());
}

}  // namespace loomia
