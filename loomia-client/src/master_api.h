// API do master server que o cliente usa.
//
// Endpoints (mesmos do site):
//   POST /api/client/login      -> { session_token, user, avatars }
//   POST /api/client/register   -> { session_token, user }
//   GET  /api/auth/me           -> { user, avatars, hasAvatar }
//   GET  /api/me/avatars        -> { avatars }
//   POST /api/me/avatars        -> { avatar }
//   PUT  /api/me/avatars/:id    -> { avatar }
//   POST /api/me/avatars/:id/activate
//   GET  /api/maps              -> [ mapas ]
//   GET  /api/maps/:id          -> mapa completo (.loom)
//   POST /api/maps/:id/launch   -> { server, token, launch_url }
#pragma once

#include <string>
#include <vector>

#include "net.h"
#include "profile.h"

namespace loomia {

struct MapSummary {
  std::string id;
  std::string name;
  std::string description;
  std::string author;
  int blocks = 0;
  int online = 0;
  bool roomOpen = false;
};

struct LoginResult {
  bool ok = false;
  std::string error;
  std::string sessionToken;
  UserInfo user;
  std::vector<AvatarInfo> avatars;
};

struct LaunchResult {
  bool ok = false;
  std::string error;
  std::string server;
  std::string token;
  std::string mapId;
};

class MasterApi {
 public:
  explicit MasterApi(HttpClient& http) : http_(http) {}

  LoginResult login(const std::string& login, const std::string& password);
  LoginResult registerAccount(const std::string& firstName, const std::string& lastName,
                              const std::string& nickname, const std::string& email,
                              const std::string& password, const std::string& passwordConfirm);

  /** Atualiza o perfil e a lista de avatares (usa o cookie atual). */
  bool refreshSession(UserInfo& user, std::vector<AvatarInfo>& avatars, std::string& error);

  bool saveAvatar(const AvatarInfo& avatar, bool create, std::string& error);
  bool activateAvatar(const std::string& id, std::string& error);

  bool listMaps(std::vector<MapSummary>& maps, std::string& error);

  /** Baixa o `.loom` desta sessão (não grava em disco permanente). */
  bool downloadMap(const std::string& mapId, std::string& json, std::string& error);

  LaunchResult launch(const std::string& mapId, const std::string& avatarId);

  /** Servidor online? (usado pelo launcher para o indicador de status). */
  bool online(int& playersOut);

 private:
  HttpClient& http_;
};

}  // namespace loomia
