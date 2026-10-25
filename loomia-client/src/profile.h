// Estado da conta no cliente: sessão, avatares e catálogo em cache.
//
// Tudo é persistido em `~/.config/loomia/client.json`, então o cliente entra
// logado na próxima abertura e o catálogo funciona offline.
#pragma once

#include <string>
#include <vector>

#include "args_parse.h"
#include "avatar.h"

namespace loomia {

struct AvatarInfo {
  std::string id;
  std::string name;
  Appearance look;
  bool active = false;
};

struct UserInfo {
  std::string id;
  std::string firstName;
  std::string lastName;
  std::string nickname;
  std::string email;
  std::string bio;
  std::string createdAt;
};


class Profile {
 public:
  bool load();
  bool save() const;

  // ---- sessão -----------------------------------------------------------
  const std::string& origin() const { return origin_; }
  void setOrigin(const std::string& origin);
  bool loggedIn() const { return !token_.empty() && !user_.id.empty(); }
  const std::string& token() const { return token_; }
  const UserInfo& user() const { return user_; }
  void setSession(const std::string& token, const UserInfo& user);
  void logout();

  // ---- avatares ---------------------------------------------------------
  const std::vector<AvatarInfo>& avatars() const { return avatars_; }
  void setAvatars(const std::vector<AvatarInfo>& avatars);
  const AvatarInfo* activeAvatar() const;
  const AvatarInfo* avatarById(const std::string& id) const;
  void setActive(const std::string& id);

  /** Aparência usada no jogo (do avatar ativo, ou padrão). */
  Appearance appearance() const;

  // ---- cache / preferências ---------------------------------------------
  std::string displayName() const;

  /** Caminho do arquivo de estado. */
  static std::string statePath();

 private:
  std::string origin_ = "http://127.0.0.1:8080";
  std::string token_;
  UserInfo user_;
  std::vector<AvatarInfo> avatars_;
};

}  // namespace loomia
