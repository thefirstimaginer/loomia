#include "master_api.h"

#include "json.h"

namespace loomia {

namespace {

Color3 numberColor(const json::ValuePtr& list, Color3 fallback) {
  if (!list || !list->isArray() || list->size() < 3) return fallback;
  Color3 color;
  color.r = static_cast<uint8_t>(clampf(static_cast<float>(list->at(0)), 0, 255));
  color.g = static_cast<uint8_t>(clampf(static_cast<float>(list->at(1)), 0, 255));
  color.b = static_cast<uint8_t>(clampf(static_cast<float>(list->at(2)), 0, 255));
  return color;
}

UserInfo readUser(const json::ValuePtr& user) {
  UserInfo info;
  if (!user) return info;
  info.id = user->str("id", "");
  info.firstName = user->str("firstName", "");
  info.lastName = user->str("lastName", "");
  info.nickname = user->str("nickname", "");
  info.email = user->str("email", "");
  info.bio = user->str("bio", "");
  info.createdAt = user->str("createdAt", "");
  return info;
}

AvatarInfo readAvatar(const json::ValuePtr& entry, const std::string& username) {
  AvatarInfo info;
  info.id = entry->str("id", "");
  info.name = entry->str("name", "Avatar");
  info.active = entry->flag("isActive", entry->flag("active", false));
  info.look.username = username;
  info.look.body = numberColor(entry->get("bodyColor"), info.look.body);
  info.look.shirt = numberColor(entry->get("shirtColor"), info.look.shirt);
  info.look.pants = numberColor(entry->get("pantsColor"), info.look.pants);
  info.look.hat = entry->str("hat", "");
  return info;
}

std::vector<AvatarInfo> readAvatars(const json::ValuePtr& list, const std::string& username) {
  std::vector<AvatarInfo> avatars;
  if (!list || !list->isArray()) return avatars;
  for (size_t index = 0; index < list->size(); index++) {
    const json::ValuePtr entry = list->item(index);
    if (entry && entry->isObject()) avatars.push_back(readAvatar(entry, username));
  }
  return avatars;
}

std::string avatarPayload(const AvatarInfo& avatar) {
  auto root = json::makeObject();
  json::set(*root, "name", json::makeString(avatar.name.empty() ? "Avatar" : avatar.name));
  json::set(*root, "bodyColor",
            json::makeVec3(avatar.look.body.r, avatar.look.body.g, avatar.look.body.b));
  json::set(*root, "shirtColor",
            json::makeVec3(avatar.look.shirt.r, avatar.look.shirt.g, avatar.look.shirt.b));
  json::set(*root, "pantsColor",
            json::makeVec3(avatar.look.pants.r, avatar.look.pants.g, avatar.look.pants.b));
  if (avatar.look.hat.empty()) {
    json::set(*root, "hat", json::makeNull());
  } else {
    json::set(*root, "hat", json::makeString(avatar.look.hat));
  }
  json::set(*root, "isActive", json::makeBool(true));
  return json::writeCompact(*root);
}

std::string errorMessage(const HttpResponse& response, const char* fallback) {
  const json::ValuePtr body = json::parse(response.body);
  if (body) {
    const std::string message = body->str("error", "");
    if (!message.empty()) return message;
  }
  if (!response.error.empty()) return response.error;
  if (response.status > 0) return std::string(fallback) + " (HTTP " + std::to_string(response.status) + ")";
  return fallback;
}

}  // namespace

LoginResult MasterApi::login(const std::string& login, const std::string& password) {
  LoginResult result;
  auto payload = json::makeObject();
  json::set(*payload, "login", json::makeString(login));
  json::set(*payload, "password", json::makeString(password));

  const HttpResponse response = http_.post("/api/client/login", json::writeCompact(*payload));
  const json::ValuePtr body = json::parse(response.body);
  if (!response.ok() || !body) {
    result.error = errorMessage(response, "não foi possível entrar");
    return result;
  }
  result.sessionToken = body->str("session_token", "");
  result.user = readUser(body->get("user"));
  result.avatars = readAvatars(body->get("avatars"), result.user.nickname);
  result.ok = !result.sessionToken.empty();
  if (!result.ok) result.error = "o servidor não devolveu a sessão";
  return result;
}

LoginResult MasterApi::registerAccount(const std::string& firstName, const std::string& lastName,
                                       const std::string& nickname, const std::string& email,
                                       const std::string& password,
                                       const std::string& passwordConfirm) {
  LoginResult result;
  auto payload = json::makeObject();
  json::set(*payload, "firstName", json::makeString(firstName));
  json::set(*payload, "lastName", json::makeString(lastName));
  json::set(*payload, "nickname", json::makeString(nickname));
  json::set(*payload, "email", json::makeString(email));
  json::set(*payload, "password", json::makeString(password));
  json::set(*payload, "passwordConfirm", json::makeString(passwordConfirm));

  const HttpResponse response = http_.post("/api/client/register", json::writeCompact(*payload));
  const json::ValuePtr body = json::parse(response.body);
  if (!response.ok() || !body) {
    result.error = errorMessage(response, "não foi possível criar a conta");
    return result;
  }
  result.sessionToken = body->str("session_token", "");
  result.user = readUser(body->get("user"));
  result.ok = !result.sessionToken.empty();
  if (!result.ok) result.error = "o servidor não devolveu a sessão";
  return result;
}

bool MasterApi::refreshSession(UserInfo& user, std::vector<AvatarInfo>& avatars, std::string& error) {
  const HttpResponse response = http_.get("/api/auth/me");
  if (!response.ok()) {
    error = errorMessage(response, "não consegui carregar o perfil");
    return false;
  }
  const json::ValuePtr body = json::parse(response.body);
  if (!body || !body->get("user")) {
    error = "resposta inválida do servidor";
    return false;
  }
  user = readUser(body->get("user"));
  avatars = readAvatars(body->get("avatars"), user.nickname);
  return true;
}

bool MasterApi::saveAvatar(const AvatarInfo& avatar, bool create, std::string& error) {
  const std::string payload = avatarPayload(avatar);
  const HttpResponse response = create ? http_.post("/api/me/avatars", payload)
                                       : http_.put("/api/me/avatars/" + avatar.id, payload);
  if (!response.ok()) {
    error = errorMessage(response, "não foi possível salvar o avatar");
    return false;
  }
  return true;
}

bool MasterApi::activateAvatar(const std::string& id, std::string& error) {
  const HttpResponse response = http_.post("/api/me/avatars/" + id + "/activate", "{}");
  if (!response.ok()) {
    error = errorMessage(response, "não foi possível ativar o avatar");
    return false;
  }
  return true;
}

bool MasterApi::listMaps(std::vector<MapSummary>& maps, std::string& error) {
  const HttpResponse response = http_.get("/api/maps");
  if (!response.ok()) {
    error = errorMessage(response, "não consegui carregar os mundos");
    return false;
  }
  const json::ValuePtr body = json::parse(response.body);
  if (!body || !body->isArray()) {
    error = "resposta inválida do catálogo";
    return false;
  }
  maps.clear();
  for (size_t index = 0; index < body->size(); index++) {
    const json::ValuePtr entry = body->item(index);
    if (!entry) continue;
    MapSummary map;
    map.id = entry->str("id", "");
    map.name = entry->str("name", map.id);
    map.description = entry->str("description", "");
    map.author = entry->str("author", "comunidade");
    map.blocks = static_cast<int>(entry->num("blocks", 0));
    map.online = static_cast<int>(entry->num("online", 0));
    map.roomOpen = entry->flag("room_open", false);
    if (!map.id.empty()) maps.push_back(map);
  }
  return true;
}

bool MasterApi::downloadMap(const std::string& mapId, std::string& jsonText, std::string& error) {
  const std::string url = http_.origin() + "/api/maps/" + mapId;
  const DownloadResult download = downloadUrl(url, http_.sessionToken());
  if (!download.ok) {
    error = download.error.empty() ? "não consegui baixar o mapa" : download.error;
    return false;
  }
  jsonText = download.content;
  return true;
}

LaunchResult MasterApi::launch(const std::string& mapId, const std::string& avatarId) {
  LaunchResult result;
  auto payload = json::makeObject();
  json::set(*payload, "mapId", json::makeString(mapId));
  if (!avatarId.empty()) json::set(*payload, "avatarId", json::makeString(avatarId));

  const HttpResponse response = http_.post("/api/maps/" + mapId + "/launch", json::writeCompact(*payload), 30);
  const json::ValuePtr body = json::parse(response.body);
  if (!response.ok() || !body) {
    result.error = errorMessage(response, "não foi possível abrir a sala");
    return result;
  }
  result.server = body->str("server", "");
  result.token = body->str("token", "");
  result.mapId = body->str("map_id", mapId);
  result.ok = !result.server.empty() && !result.token.empty();
  if (!result.ok) result.error = "a sala não devolveu endereço/token";
  return result;
}

bool MasterApi::online(int& playersOut) {
  const HttpResponse response = http_.get("/api/status", 4);
  playersOut = 0;
  if (!response.ok()) return false;
  const json::ValuePtr body = json::parse(response.body);
  if (body) playersOut = static_cast<int>(body->num("players_online", 0));
  return true;
}

}  // namespace loomia
