#include "profile.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include "json.h"

#ifdef _WIN32
#include <direct.h>
#else
#include <sys/stat.h>
#endif

namespace loomia {

namespace {

std::string configDir() {
#ifdef _WIN32
  const char* appData = std::getenv("APPDATA");
  std::string base = appData ? appData : ".";
  std::string dir = base + "\\Loomia";
  _mkdir(dir.c_str());
  return dir;
#else
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  const char* home = std::getenv("HOME");
  std::string base = xdg ? xdg : (home ? std::string(home) + "/.config" : ".");
  std::string dir = base + "/loomia";
  mkdir(base.c_str(), 0755);
  mkdir(dir.c_str(), 0755);
  return dir;
#endif
}

Color3 colorFromJson(const json::ValuePtr& value, Color3 fallback) {
  if (!value || !value->isArray() || value->size() < 3) return fallback;
  Color3 color;
  color.r = static_cast<uint8_t>(clampf(static_cast<float>(value->at(0)), 0, 255));
  color.g = static_cast<uint8_t>(clampf(static_cast<float>(value->at(1)), 0, 255));
  color.b = static_cast<uint8_t>(clampf(static_cast<float>(value->at(2)), 0, 255));
  return color;
}

const char* envOr(const char* name, const char* fallback) {
  const char* value = std::getenv(name);
  return (value && *value) ? value : fallback;
}

}  // namespace

std::string Profile::statePath() { return configDir() + "/client.json"; }

void Profile::setOrigin(const std::string& origin) {
  origin_ = origin;
  while (!origin_.empty() && origin_.back() == '/') origin_.pop_back();
}

bool Profile::load() {
  std::ifstream file(statePath());
  if (!file) {
    // Primeira execução: origem do servidor pode vir do ambiente (dev/testes).
    origin_ = envOr("LOOMIA_MASTER", origin_.c_str());
    return false;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  const json::ValuePtr root = json::parse(buffer.str());
  if (!root) return false;

  origin_ = root->str("origin", origin_);
  token_ = root->str("token", "");
  if (const json::ValuePtr user = root->get("user")) {
    user_.id = user->str("id", "");
    user_.firstName = user->str("firstName", "");
    user_.lastName = user->str("lastName", "");
    user_.nickname = user->str("nickname", "");
    user_.email = user->str("email", "");
    user_.bio = user->str("bio", "");
    user_.createdAt = user->str("createdAt", "");
  }
  avatars_.clear();
  if (const json::ValuePtr list = root->get("avatars")) {
    if (list->isArray()) {
      for (size_t index = 0; index < list->size(); index++) {
        const json::ValuePtr entry = list->item(index);
        if (!entry) continue;
        AvatarInfo info;
        info.id = entry->str("id", "");
        info.name = entry->str("name", "Avatar");
        info.active = entry->flag("active", false);
        info.look.username = user_.nickname;
        info.look.body = colorFromJson(entry->get("bodyColor"), info.look.body);
        info.look.shirt = colorFromJson(entry->get("shirtColor"), info.look.shirt);
        info.look.pants = colorFromJson(entry->get("pantsColor"), info.look.pants);
        info.look.hat = entry->str("hat", "");
        avatars_.push_back(info);
      }
    }
  }
  return true;
}

bool Profile::save() const {
  auto root = json::makeObject();
  json::set(*root, "origin", json::makeString(origin_));
  json::set(*root, "token", json::makeString(token_));

  auto user = json::makeObject();
  json::set(*user, "id", json::makeString(user_.id));
  json::set(*user, "firstName", json::makeString(user_.firstName));
  json::set(*user, "lastName", json::makeString(user_.lastName));
  json::set(*user, "nickname", json::makeString(user_.nickname));
  json::set(*user, "email", json::makeString(user_.email));
  json::set(*user, "bio", json::makeString(user_.bio));
  json::set(*user, "createdAt", json::makeString(user_.createdAt));
  json::set(*root, "user", user);

  auto avatars = json::makeArray();
  for (const AvatarInfo& info : avatars_) {
    auto entry = json::makeObject();
    json::set(*entry, "id", json::makeString(info.id));
    json::set(*entry, "name", json::makeString(info.name));
    json::set(*entry, "active", json::makeBool(info.active));
    json::set(*entry, "bodyColor", json::makeVec3(info.look.body.r, info.look.body.g, info.look.body.b));
    json::set(*entry, "shirtColor", json::makeVec3(info.look.shirt.r, info.look.shirt.g, info.look.shirt.b));
    json::set(*entry, "pantsColor", json::makeVec3(info.look.pants.r, info.look.pants.g, info.look.pants.b));
    if (!info.look.hat.empty()) json::set(*entry, "hat", json::makeString(info.look.hat));
    json::push(*avatars, entry);
  }
  json::set(*root, "avatars", avatars);

  std::ofstream file(statePath(), std::ios::trunc);
  if (!file) return false;
  file << json::write(*root, true);
  return static_cast<bool>(file);
}

void Profile::setSession(const std::string& token, const UserInfo& user) {
  token_ = token;
  user_ = user;
}

void Profile::logout() {
  token_.clear();
  user_ = UserInfo{};
  avatars_.clear();
}

void Profile::setAvatars(const std::vector<AvatarInfo>& avatars) { avatars_ = avatars; }

const AvatarInfo* Profile::activeAvatar() const {
  for (const AvatarInfo& info : avatars_) {
    if (info.active) return &info;
  }
  return avatars_.empty() ? nullptr : &avatars_.front();
}

const AvatarInfo* Profile::avatarById(const std::string& id) const {
  for (const AvatarInfo& info : avatars_) {
    if (info.id == id) return &info;
  }
  return nullptr;
}

void Profile::setActive(const std::string& id) {
  for (AvatarInfo& info : avatars_) info.active = (info.id == id);
}

Appearance Profile::appearance() const {
  Appearance look;
  look.username = displayName();
  if (const AvatarInfo* avatar = activeAvatar()) {
    look = avatar->look;
    look.username = displayName();
  }
  return look;
}

std::string Profile::displayName() const {
  if (!user_.nickname.empty()) return user_.nickname;
  if (!user_.firstName.empty()) return user_.firstName;
  return "Loomian";
}

}  // namespace loomia
