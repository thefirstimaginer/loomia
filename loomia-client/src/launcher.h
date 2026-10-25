// Launcher: a interface que fica aberta enquanto o jogador navega no site.
//
// Abas: Conta (login/cadastro), Perfil (dados + avatares) e Avatar (editor).
// A peça desenhada é o avatar em 3D (SFML para o 2D, OpenGL para o boneco).
#pragma once

#include <SFML/Graphics.hpp>

#include <optional>
#include <string>
#include <vector>

#include "args_parse.h"
#include "master_api.h"
#include "profile.h"
#include "renderer.h"

namespace loomia {

enum class LauncherTab { Account, Profile, Avatar };

enum class LauncherResult {
  None,     // continua na janela
  Quit,     // fechar o cliente
  Play,     // o jogador pediu para entrar no mapa (`launch` preenchido)
};

class Launcher {
 public:
  Launcher(sf::RenderWindow& window, Profile& profile, MasterApi& api, HttpClient& http);

  /** Carrega fontes. Devolve false + erro se não achar o arquivo. */
  bool init(std::string& error);

  /** Processa eventos/UI e desenha um frame. */
  LauncherResult frame(const std::optional<sf::Event>& event, float dt);

  /** O link chegou (site chamou loomia://): entra direto no mapa. */
  void queueLaunch(const LaunchInfo& launch);
  bool hasPendingLaunch(LaunchInfo& launch);

  const std::string& message() const { return message_; }

  /** A build default espera login antes de entrar no mapa. */
  void requireLogin(const std::string& message);
  /** Remove o launch da fila (chamado quando o jogo realmente abre). */
  void popPendingLaunch();

 private:
  void draw(const sf::RenderTarget& target);
  void drawAccount(const sf::FloatRect& area);
  void drawProfile(const sf::FloatRect& area);
  void drawAvatarEditor(const sf::FloatRect& area);
  void drawAvatarPreview(sf::RenderTarget& target, const sf::FloatRect& area, const AvatarInfo& avatar);
  void flushPreviews();
  void drawField(const sf::FloatRect& area, const std::string& value, const std::string& placeholder,
                 bool secret);
  void drawButton(const sf::FloatRect& area, const std::string& label, bool primary, bool hovered,
                  bool enabled = true);

  /** Áreas clicáveis do último frame (campos e botões). */
  struct FieldHit {
    std::string id;
    sf::FloatRect rect;
  };
  struct ButtonHit {
    std::string id;
    sf::FloatRect rect;
    bool enabled = true;
  };
  std::vector<FieldHit> fieldRects_;
  std::vector<ButtonHit> buttonRects_;

  void refresh();
  void doLogin();
  void doRegister();
  void startEdit(const AvatarInfo* avatar);
  void saveAvatar();

  sf::RenderWindow& window_;
  Profile& profile_;
  MasterApi& api_;
  HttpClient& http_;

  sf::Font font_;
  sf::Font bold_;
  bool ready_ = false;

  LauncherTab tab_ = LauncherTab::Account;
  bool registerMode_ = false;
  std::string message_;
  bool messageError_ = false;

  // Campos de texto (editados via `event`).
  std::string fieldLogin_, fieldPassword_;
  std::string fieldFirst_, fieldLast_, fieldNick_, fieldEmail_, fieldPassword2_;
  std::string focused_;

  // Editor de avatar.
  bool editing_ = false;
  AvatarInfo draft_;
  int bodyIndex_ = 0;
  int shirtIndex_ = 0;
  int pantsIndex_ = 0;
  int hatIndex_ = 0;

  Vec3 cameraYaw_{0, 0.5f, 18};
  float spin_ = 0;
  bool serverOnline_ = false;
  int onlinePlayers_ = 0;
  float statusTimer_ = 0;

  std::vector<LaunchInfo> pendingLaunches_;
  bool pendingLogin_ = false;
  std::string lastRefreshed_;

  // Clique pendente: resolvido depois do desenho (as áreas são do frame).
  sf::Vector2f pendingClick_{0, 0};
  bool hasClick_ = false;
  void processClick(const sf::Vector2f& point);

  WorldRenderer preview_;
  bool previewReady_ = false;
  std::string previewError_;
  struct PreviewJob {
    sf::FloatRect area;
    AvatarInfo avatar;
  };
  std::vector<PreviewJob> glQueue_;
};

}  // namespace loomia
