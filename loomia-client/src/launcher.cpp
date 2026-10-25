#include "launcher.h"

#include <glad/glad.h>

#include <algorithm>
#include <cstdio>
#include <functional>

namespace loomia {

namespace {

// Paleta do site do Loomia (mesma identidade visual).
// Paleta do site do Loomia (identidade visual do frontend, não do admin):
// papel claro #f7f4ec, tinta #16202c, amarelo Loomia #e9b52b, bordas grossas.
const sf::Color kBackground(247, 244, 236);   // --paper
const sf::Color kPanel(255, 255, 255);        // --panel
const sf::Color kPanelSoft(255, 253, 247);    // campos (#fffdf7)
const sf::Color kLine(22, 32, 44);            // --ink (bordas)
const sf::Color kText(22, 32, 44);            // --ink
const sf::Color kMuted(76, 91, 108);          // --ink-soft
const sf::Color kAccent(233, 181, 43);        // --loomia
const sf::Color kAccentDark(200, 147, 26);    // --loomia-dark
const sf::Color kOk(46, 139, 87);             // --ok
const sf::Color kDanger(192, 57, 43);         // --danger
const sf::Color kGold(233, 181, 43);          // --loomia

const std::vector<Color3> kBodyColors = {{234, 190, 25}, {224, 175, 140}, {140, 90, 60},
                                         {245, 210, 170}, {120, 200, 140}, {190, 190, 200}};
const std::vector<Color3> kShirtColors = {{15, 120, 190}, {190, 40, 40}, {230, 230, 230},
                                          {70, 70, 70},  {240, 160, 20}, {110, 60, 160}};
const std::vector<Color3> kPantsColors = {{35, 75, 45}, {30, 30, 40}, {90, 70, 50},
                                          {60, 60, 120}, {120, 120, 120}};
const std::vector<std::string> kHats = {"", "cap", "top_hat", "propeller", "crown"};
const std::vector<std::string> kHatLabels = {"Sem chapéu", "Boné", "Cartola", "Hélice", "Coroa"};

std::string clip(const std::string& text, size_t max) {
  return text.size() <= max ? text : text.substr(0, max - 1) + "...";
}

sf::Color toColor(const Color3& color) { return sf::Color(color.r, color.g, color.b); }

sf::FloatRect rect(float x, float y, float width, float height) {
  return sf::FloatRect(sf::Vector2f(x, y), sf::Vector2f(width, height));
}

bool loadFont(sf::Font& font) {
  const char* candidates[] = {
      "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/TTF/DejaVuSans.ttf",
      "/usr/share/fonts/dejavu/DejaVuSans.ttf",
      "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
      "/System/Library/Fonts/Supplemental/Arial.ttf",
      "C:/Windows/Fonts/segoeui.ttf",
      "C:/Windows/Fonts/arial.ttf",
  };
  for (const char* path : candidates) {
    if (font.openFromFile(path)) return true;
  }
  return false;
}

void drawText(sf::RenderTarget& target, const sf::Font& font, const std::string& text, float x, float y,
              unsigned size, sf::Color color) {
  sf::Text label(font, text, size);
  label.setFillColor(color);
  label.setPosition(sf::Vector2f(x, y));
  target.draw(label);
}

void drawPanel(sf::RenderTarget& target, const sf::FloatRect& area, sf::Color fill, sf::Color outline,
               float thickness = 1.f) {
  sf::RectangleShape shape(area.size);
  shape.setPosition(area.position);
  shape.setFillColor(fill);
  shape.setOutlineThickness(thickness);
  shape.setOutlineColor(outline);
  target.draw(shape);
}

void drawSwatch(sf::RenderTarget& target, float x, float y, const Color3& color, bool selected) {
  sf::RectangleShape swatch(sf::Vector2f(34, 34));
  swatch.setPosition(sf::Vector2f(x, y));
  swatch.setFillColor(toColor(color));
  swatch.setOutlineThickness(selected ? 3.f : 1.f);
  swatch.setOutlineColor(selected ? kText : kLine);
  target.draw(swatch);
}

}  // namespace

Launcher::Launcher(sf::RenderWindow& window, Profile& profile, MasterApi& api, HttpClient& http)
    : window_(window), profile_(profile), api_(api), http_(http) {}

bool Launcher::init(std::string& error) {
  if (!loadFont(font_)) {
    error = "não encontrei uma fonte do sistema (instale fonts-dejavu-core)";
    return false;
  }
  std::string rendererError;
  previewReady_ = preview_.init(rendererError);
  if (!previewReady_) previewError_ = rendererError;
  ready_ = true;
  return true;
}

void Launcher::queueLaunch(const LaunchInfo& launch) {
  if (!launch.valid) return;
  pendingLaunches_.push_back(launch);
}

bool Launcher::hasPendingLaunch(LaunchInfo& launch) {
  if (pendingLaunches_.empty()) return false;
  launch = pendingLaunches_.front();
  return true;
}

void Launcher::requireLogin(const std::string& message) {
  // A build default não entra no mapa sem login: mostra a aba Conta com a
  // mensagem. O launch continua na fila (hasPendingLaunch não consome) — quando
  // o login completar, o jogo abre sozinho. `pendingLogin_` trava re-chamadas
  // (a mensagem ficaria piscando a cada frame).
  if (pendingLogin_) return;
  pendingLogin_ = true;
  tab_ = LauncherTab::Account;
  registerMode_ = false;
  focused_.clear();
  message_ = message;
  messageError_ = false;
}

void Launcher::popPendingLaunch() {
  if (!pendingLaunches_.empty()) pendingLaunches_.erase(pendingLaunches_.begin());
  pendingLogin_ = false;
}

void Launcher::refresh() {
  if (!profile_.loggedIn()) return;
  UserInfo user;
  std::vector<AvatarInfo> avatars;
  std::string error;
  if (api_.refreshSession(user, avatars, error)) {
    profile_.setSession(profile_.token(), user);
    profile_.setAvatars(avatars);
    profile_.save();
    return;
  }
  if (error.find("401") != std::string::npos) {
    profile_.logout();
    profile_.save();
    message_ = "sessão expirada: entre de novo";
    messageError_ = true;
    tab_ = LauncherTab::Account;
  }
}

void Launcher::doLogin() {
  message_.clear();
  if (fieldLogin_.empty() || fieldPassword_.empty()) {
    message_ = "informe email/apelido e senha";
    messageError_ = true;
    return;
  }
  const LoginResult result = api_.login(fieldLogin_, fieldPassword_);
  if (!result.ok) {
    message_ = result.error;
    messageError_ = true;
    return;
  }
  http_.setSessionToken("loomia_session=" + result.sessionToken);
  profile_.setSession(result.sessionToken, result.user);
  profile_.setAvatars(result.avatars);
  profile_.save();
  fieldPassword_.clear();
  pendingLogin_ = false;
  message_ = "bem-vindo, " + result.user.nickname + "!";
  messageError_ = false;
  if (result.avatars.empty()) {
    startEdit(nullptr);
  } else {
    tab_ = LauncherTab::Profile;
  }
}

void Launcher::doRegister() {
  message_.clear();
  const LoginResult result = api_.registerAccount(fieldFirst_, fieldLast_, fieldNick_, fieldEmail_,
                                                  fieldPassword_, fieldPassword2_);
  if (!result.ok) {
    message_ = result.error;
    messageError_ = true;
    return;
  }
  http_.setSessionToken("loomia_session=" + result.sessionToken);
  profile_.setSession(result.sessionToken, result.user);
  profile_.setAvatars({});
  profile_.save();
  message_ = "conta criada! agora monte seu avatar";
  messageError_ = false;
  registerMode_ = false;
  startEdit(nullptr);
}

void Launcher::startEdit(const AvatarInfo* avatar) {
  editing_ = true;
  tab_ = LauncherTab::Avatar;
  if (avatar) {
    draft_ = *avatar;
  } else {
    draft_ = AvatarInfo{};
    draft_.id.clear();
    draft_.name = "Meu avatar";
    draft_.look = profile_.appearance();
  }

  auto indexOf = [](const std::vector<Color3>& list, const Color3& color) {
    for (size_t i = 0; i < list.size(); i++) {
      if (list[i].r == color.r && list[i].g == color.g && list[i].b == color.b) return static_cast<int>(i);
    }
    return 0;
  };
  bodyIndex_ = indexOf(kBodyColors, draft_.look.body);
  shirtIndex_ = indexOf(kShirtColors, draft_.look.shirt);
  pantsIndex_ = indexOf(kPantsColors, draft_.look.pants);
  hatIndex_ = 0;
  for (size_t i = 0; i < kHats.size(); i++) {
    if (kHats[i] == draft_.look.hat) hatIndex_ = static_cast<int>(i);
  }
}

void Launcher::saveAvatar() {
  message_.clear();
  if (!profile_.loggedIn()) {
    message_ = "entre na sua conta primeiro";
    messageError_ = true;
    return;
  }
  draft_.look.body = kBodyColors[static_cast<size_t>(bodyIndex_)];
  draft_.look.shirt = kShirtColors[static_cast<size_t>(shirtIndex_)];
  draft_.look.pants = kPantsColors[static_cast<size_t>(pantsIndex_)];
  draft_.look.hat = kHats[static_cast<size_t>(hatIndex_)];
  draft_.look.username = profile_.displayName();

  std::string error;
  const bool create = draft_.id.empty();
  if (!api_.saveAvatar(draft_, create, error)) {
    message_ = error;
    messageError_ = true;
    return;
  }
  refresh();
  message_ = "avatar salvo!";
  messageError_ = false;
  editing_ = false;
  tab_ = LauncherTab::Profile;
}

// ---------------------------------------------------------------------------
// Desenho
// ---------------------------------------------------------------------------

void Launcher::drawAvatarPreview(sf::RenderTarget& target, const sf::FloatRect& area,
                                 const AvatarInfo& avatar) {
  if (!previewReady_) {
    drawPanel(target, area, kPanel, kLine);
    drawText(target, font_, clip("3D: " + previewError_, 40), area.position.x + 10, area.position.y + 10, 12,
             kMuted);
    return;
  }
  if (area.size.x < 4 || area.size.y < 4) return;
  // Sem painel branco: ele seria desenhado POR CIMA do 3D no display() (o SFML
  // envia a UI 2D depois do OpenGL cru). Só a moldura, translúcida.
  sf::RectangleShape frame(area.size);
  frame.setPosition(area.position);
  frame.setFillColor(sf::Color::Transparent);
  frame.setOutlineThickness(1.f);
  frame.setOutlineColor(kLine);
  target.draw(frame);
  // Adiado: o 3D é desenhado no INÍCIO do PRÓXIMO frame (antes da UI 2D).
  glQueue_.push_back({area, avatar});
}

void Launcher::flushPreviews() {
  // O OpenGL cru destrói o estado que o SFML usa para a UI (viewport, depth,
  // blend, textura da fonte): o texto virava um bloco preto. O fundo e os
  // previews são desenhados aqui, ANTES da fila 2D do SFML, e o estado volta
  // com resetGLStates().
  const sf::Vector2u size = window_.getSize();
  glViewport(0, 0, static_cast<int>(size.x), static_cast<int>(size.y));
  glDisable(GL_DEPTH_TEST);

  for (const PreviewJob& job : glQueue_) {
    const sf::FloatRect& area = job.area;
    // glClear ignora o viewport: sem scissor, cada card APAGARIA o boneco do
    // card anterior (e o próprio, se a UI fosse desenhada depois).
    const int cardY = static_cast<int>(static_cast<float>(size.y) - area.position.y - area.size.y);
    glScissor(static_cast<int>(area.position.x), cardY,
              static_cast<int>(area.size.x), static_cast<int>(area.size.y));
    glEnable(GL_SCISSOR_TEST);
    glViewport(static_cast<int>(area.position.x), cardY,
               static_cast<int>(area.size.x), static_cast<int>(area.size.y));
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.97f, 0.96f, 0.93f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_SCISSOR_TEST);

    Camera camera;
    camera.position = Vec3{2.8f, 3.4f, 6.2f};
    camera.target = Vec3{0, 2.0f, 0};
    camera.fovDegrees = 42;
    const float aspect = area.size.x / std::max(1.0f, area.size.y);

    Avatar model;
    AvatarInfo shown = job.avatar;
    model.setup(shown.look);
    model.update(Vec3{0, 0, 0}, spin_, 0, 0);
    model.draw(preview_, camera, aspect);
  }
  glQueue_.clear();
  window_.resetGLStates();
}

void Launcher::drawField(const sf::FloatRect& area, const std::string& value,
                         const std::string& placeholder, bool secret) {
  drawPanel(window_, area, kPanelSoft, kLine);
  std::string shown = value;
  if (secret) shown = std::string(value.size(), '*');
  const bool empty = shown.empty();
  if (empty) shown = placeholder;
  drawText(window_, font_, clip(shown, 60), area.position.x + 12, area.position.y + 9, 15,
           empty ? kMuted : kText);
}

void Launcher::drawButton(const sf::FloatRect& area, const std::string& label, bool primary,
                          bool hovered, bool enabled) {
  sf::Color fill = kPanelSoft;
  if (!enabled) fill = kPanel;
  else if (primary) fill = hovered ? kAccentDark : kAccent;
  else if (hovered) fill = kLine;
  drawPanel(window_, area, fill, kLine);

  const std::string shown = clip(label, 32);
  const float width = static_cast<float>(shown.size()) * 8.2f;
  sf::Text text(font_, shown, 15);
  text.setFillColor(primary ? sf::Color(22, 32, 44) : (enabled ? kText : kMuted));
  const sf::FloatRect bounds = text.getLocalBounds();
  text.setPosition(sf::Vector2f(area.position.x + (area.size.x - width) / 2 - bounds.position.x,
                                area.position.y + (area.size.y - bounds.size.y) / 2 - bounds.position.y));
  window_.draw(text);
}

// ---------------------------------------------------------------------------
// Abas
// ---------------------------------------------------------------------------

void Launcher::drawAccount(const sf::FloatRect& area) {
  const float width = std::min(area.size.x - 80.f, 520.f);
  const float x = area.position.x + (area.size.x - width) / 2;
  float y = area.position.y + 30;

  drawText(window_, font_, registerMode_ ? "Criar conta" : "Entrar na sua conta", x, y, 26, kText);
  y += 42;
  drawText(window_, font_, "O client precisa da conta para entrar nos mundos.", x, y, 13, kMuted);
  y += 30;

  fieldRects_.clear();
  if (registerMode_) {
    fieldRects_.push_back({"first", rect(x, y, (width - 12) / 2, 38)});
    fieldRects_.push_back({"last", rect(x + (width + 12) / 2, y, (width - 12) / 2, 38)});
    drawField(rect(x, y, (width - 12) / 2, 38), fieldFirst_, "nome", false);
    drawField(rect(x + (width + 12) / 2, y, (width - 12) / 2, 38), fieldLast_, "sobrenome", false);
    y += 50;
    fieldRects_.push_back({"nick", rect(x, y, (width - 12) / 2, 38)});
    fieldRects_.push_back({"email", rect(x + (width + 12) / 2, y, (width - 12) / 2, 38)});
    drawField(rect(x, y, (width - 12) / 2, 38), fieldNick_, "apelido", false);
    drawField(rect(x + (width + 12) / 2, y, (width - 12) / 2, 38), fieldEmail_, "email", false);
    y += 50;
    fieldRects_.push_back({"password", rect(x, y, width, 38)});
    drawField(rect(x, y, width, 38), fieldPassword_, "senha (mín. 8)", true);
    y += 50;
    fieldRects_.push_back({"password2", rect(x, y, width, 38)});
    drawField(rect(x, y, width, 38), fieldPassword2_, "repetir senha", true);
    y += 54;
  } else {
    fieldRects_.push_back({"login", rect(x, y, width, 38)});
    drawField(rect(x, y, width, 38), fieldLogin_, "email ou apelido", false);
    y += 50;
    fieldRects_.push_back({"password", rect(x, y, width, 38)});
    drawField(rect(x, y, width, 38), fieldPassword_, "senha", true);
    y += 54;
  }

  buttonRects_.clear();
  buttonRects_.push_back({"submit", rect(x, y, width, 44), true});
  drawButton(rect(x, y, width, 44), registerMode_ ? "CRIAR CONTA" : "ENTRAR", true, false);
  y += 56;
  buttonRects_.push_back({"toggle", rect(x, y, width, 34), true});
  drawButton(rect(x, y, width, 34), registerMode_ ? "Já tenho conta" : "Criar uma conta nova", false, false);
}

void Launcher::drawProfile(const sf::FloatRect& area) {
  const float left = area.position.x;
  float y = area.position.y + 16;

  drawText(window_, font_, clip(profile_.displayName(), 30), left, y, 26, kText);
  y += 34;
  const UserInfo& user = profile_.user();
  const std::string about = user.firstName + " + " + user.lastName + "  |  " + user.email;
  drawText(window_, font_, clip(about, 70), left, y, 13, kMuted);

  drawText(window_, font_, "Seus avatares", left, y + 34, 17, kText);

  buttonRects_.clear();
  buttonRects_.push_back({"logout", rect(area.position.x + area.size.x - 130, area.position.y + 16, 110, 32), true});
  drawButton(rect(area.position.x + area.size.x - 130, area.position.y + 16, 110, 32), "SAIR", false, false);

  const std::vector<AvatarInfo>& avatars = profile_.avatars();
  float cardX = left;
  float cardY = y + 64;
  const float cardWidth = 210, cardHeight = 260;
  for (size_t index = 0; index < avatars.size(); index++) {
    const AvatarInfo& avatar = avatars[index];
    drawPanel(window_, rect(cardX, cardY, cardWidth, cardHeight), kPanelSoft,
              avatar.active ? kOk : kLine, avatar.active ? 2.f : 1.f);
    drawAvatarPreview(window_, rect(cardX + 10, cardY + 10, cardWidth - 20, 170), avatar);
    drawText(window_, font_, clip(avatar.name, 22), cardX + 12, cardY + 188, 14, kText);
    if (avatar.active) drawText(window_, font_, "ativo", cardX + cardWidth - 44, cardY + 188, 12, kOk);

    drawButton(rect(cardX + 10, cardY + 215, 90, 30), avatar.active ? "ATIVO" : "USAR", avatar.active, false);
    drawButton(rect(cardX + 108, cardY + 215, 90, 30), "EDITAR", false, false);
    buttonRects_.push_back({"activate:" + avatar.id, rect(cardX + 10, cardY + 215, 90, 30), !avatar.active});
    buttonRects_.push_back({"edit:" + avatar.id, rect(cardX + 108, cardY + 215, 90, 30), true});

    cardX += cardWidth + 16;
    if (cardX + cardWidth > area.position.x + area.size.x) {
      cardX = left;
      cardY += cardHeight + 16;
    }
  }

  if (avatars.empty()) {
    drawText(window_, font_, "Nenhum avatar ainda: crie o primeiro.", left, cardY + 10, 15, kMuted);
  }
  const float newY = area.position.y + area.size.y - 56;
  drawButton(rect(left, newY, 200, 40), "CRIAR AVATAR", true, false);
  buttonRects_.push_back({"new", rect(left, newY, 200, 40), true});
}

void Launcher::drawAvatarEditor(const sf::FloatRect& area) {
  const float previewWidth = 280;
  const float left = area.position.x;
  float y = area.position.y + 16;

  AvatarInfo shown = draft_;
  shown.look.body = kBodyColors[static_cast<size_t>(bodyIndex_)];
  shown.look.shirt = kShirtColors[static_cast<size_t>(shirtIndex_)];
  shown.look.pants = kPantsColors[static_cast<size_t>(pantsIndex_)];
  shown.look.hat = kHats[static_cast<size_t>(hatIndex_)];

  drawAvatarPreview(window_, rect(left, y, previewWidth, 340), shown);
  drawField(rect(left, y + 350, previewWidth, 36), shown.name, "nome do avatar", false);
  fieldRects_.clear();
  fieldRects_.push_back({"name", rect(left, y + 350, previewWidth, 36)});

  const float formX = left + previewWidth + 30;
  float formY = y;
  drawText(window_, font_, draft_.id.empty() ? "Novo avatar" : "Editar avatar", formX, formY, 24, kText);
  formY += 46;

  buttonRects_.clear();
  auto colorRow = [&](const std::string& label, const char* prefix, const std::vector<Color3>& colors,
                      int index) {
    drawText(window_, font_, label, formX, formY, 12, kMuted);
    formY += 20;
    for (size_t i = 0; i < colors.size(); i++) {
      const float swatchX = formX + static_cast<float>(i) * 42.f;
      drawSwatch(window_, swatchX, formY, colors[i], static_cast<int>(i) == index);
      buttonRects_.push_back({std::string(prefix) + ":" + std::to_string(i), rect(swatchX, formY, 34, 34), true});
    }
    formY += 48;
  };
  colorRow("PELE", "body", kBodyColors, bodyIndex_);
  colorRow("CAMISA", "shirt", kShirtColors, shirtIndex_);
  colorRow("CALCA", "pants", kPantsColors, pantsIndex_);

  drawText(window_, font_, "CHAPEU", formX, formY, 12, kMuted);
  formY += 20;
  float chipX = formX;
  for (size_t i = 0; i < kHatLabels.size(); i++) {
    const float chipWidth = static_cast<float>(kHatLabels[i].size()) * 7.6f + 24.f;
    const bool selected = static_cast<int>(i) == hatIndex_;
    drawPanel(window_, rect(chipX, formY, chipWidth, 30), selected ? kGold : kPanelSoft, kLine);
    sf::Text text(font_, kHatLabels[i], 12);
    text.setFillColor(selected ? sf::Color(22, 32, 44) : kMuted);
    text.setPosition(sf::Vector2f(chipX + 10, formY + 7));
    window_.draw(text);
    buttonRects_.push_back({"hat:" + std::to_string(i), rect(chipX, formY, chipWidth, 30), true});
    chipX += chipWidth + 8;
  }
  formY += 52;

  drawButton(rect(formX, formY, 210, 42), "SALVAR AVATAR", true, false);
  drawButton(rect(formX + 222, formY, 150, 42), "CANCELAR", false, false);
  buttonRects_.push_back({"save", rect(formX, formY, 210, 42), true});
  buttonRects_.push_back({"cancel", rect(formX + 222, formY, 150, 42), true});
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

LauncherResult Launcher::frame(const std::optional<sf::Event>& event, float dt) {
  spin_ += dt * 0.6f;
  statusTimer_ += dt;
  if (statusTimer_ > 6.f) {
    statusTimer_ = 0;
    int players = 0;
    serverOnline_ = api_.online(players);
    onlinePlayers_ = players;
  }

  const sf::Vector2u size = window_.getSize();
  const sf::FloatRect area(sf::Vector2f(40.f, 110.f),
                           sf::Vector2f(static_cast<float>(size.x) - 80.f, static_cast<float>(size.y) - 180.f));

  // ---- eventos ----------------------------------------------------------
  if (event) {
    if (event->is<sf::Event::Closed>()) return LauncherResult::Quit;

    if (const auto* text = event->getIf<sf::Event::TextEntered>()) {
      const uint32_t code = text->unicode;
      if (code == 8) {
        std::string* target = &fieldLogin_;
        if (focused_ == "first") target = &fieldFirst_;
        else if (focused_ == "last") target = &fieldLast_;
        else if (focused_ == "nick") target = &fieldNick_;
        else if (focused_ == "email") target = &fieldEmail_;
        else if (focused_ == "password") target = &fieldPassword_;
        else if (focused_ == "password2") target = &fieldPassword2_;
        else if (focused_ == "name") target = &draft_.name;
        if (!target->empty()) target->pop_back();
      } else if (code >= 32 && code < 127) {
        const char c = static_cast<char>(code);
        if (focused_ == "first") fieldFirst_ += c;
        else if (focused_ == "last") fieldLast_ += c;
        else if (focused_ == "nick") fieldNick_ += c;
        else if (focused_ == "email") fieldEmail_ += c;
        else if (focused_ == "password") fieldPassword_ += c;
        else if (focused_ == "password2") fieldPassword2_ += c;
        else if (focused_ == "login") fieldLogin_ += c;
        else if (focused_ == "name") draft_.name += c;
      }
    }

    if (const auto* key = event->getIf<sf::Event::KeyPressed>()) {
      if (key->code == sf::Keyboard::Key::Escape) return LauncherResult::Quit;
      if (key->code == sf::Keyboard::Key::Tab) focused_.clear();
      if (key->code == sf::Keyboard::Key::Enter) {
        if (tab_ == LauncherTab::Avatar) saveAvatar();
        else if (registerMode_) doRegister();
        else doLogin();
      }
    }

    if (const auto* mouse = event->getIf<sf::Event::MouseButtonPressed>()) {
      if (mouse->button == sf::Mouse::Button::Left) {
        // Guarda o ponto: as áreas clicáveis só existem depois do desenho.
        pendingClick_ = sf::Vector2f(static_cast<float>(mouse->position.x),
                                     static_cast<float>(mouse->position.y));
        hasClick_ = true;
      }
    }
  }

  // ---- desenho (monta fieldRects_/buttonRects_ do frame) ----------------
  window_.clear(kBackground);

  sf::RectangleShape bar(sf::Vector2f(static_cast<float>(size.x), 64.f));
  bar.setFillColor(kPanel);
  window_.draw(bar);
  drawText(window_, font_, "LOOMIA", 28, 16, 24, kGold);
  drawText(window_, font_, "client " + profile_.displayName(), 146, 24, 12, kMuted);

  sf::CircleShape dot(5);
  dot.setPosition(sf::Vector2f(static_cast<float>(size.x) - 300, 27));
  dot.setFillColor(serverOnline_ ? kOk : kDanger);
  window_.draw(dot);
  drawText(window_, font_,
           serverOnline_ ? "online - " + std::to_string(onlinePlayers_) + " jogando" : "offline",
           static_cast<float>(size.x) - 288, 24, 13, serverOnline_ ? kOk : kDanger);

  // Abas.
  buttonRects_.clear();
  float tabX = 300;
  struct TabDef {
    LauncherTab tab;
    const char* label;
    bool enabled;
  };
  const TabDef tabs[] = {{LauncherTab::Account, "Conta", true},
                         {LauncherTab::Profile, "Perfil", profile_.loggedIn()},
                         {LauncherTab::Avatar, "Avatar", profile_.loggedIn()}};
  for (const TabDef& def : tabs) {
    const float width = static_cast<float>(std::string(def.label).size()) * 9.f + 26.f;
    const bool active = (tab_ == def.tab) && (def.enabled);
    drawPanel(window_, rect(tabX, 16, width, 32), active ? kAccent : kPanelSoft, kLine);
    sf::Text label(font_, def.label, 14);
    label.setFillColor(active ? sf::Color(22, 32, 44) : (def.enabled ? kText : kMuted));
    label.setPosition(sf::Vector2f(tabX + 13, 22));
    window_.draw(label);
    if (def.enabled) {
      const LauncherTab target = def.tab;
      // Guardado para o próximo frame (o botão é resolvido por id).
      buttonRects_.push_back({"tab:" + std::to_string(static_cast<int>(target)), rect(tabX, 16, width, 32), true});
    }
    tabX += width + 8;
  }

  if (!profile_.loggedIn() || tab_ == LauncherTab::Account) {
    drawAccount(area);
  } else if (tab_ == LauncherTab::Profile) {
    drawProfile(area);
  } else {
    drawAvatarEditor(area);
  }

  // Rodapé: mensagem ou instrução do link.
  if (!message_.empty()) {
    drawPanel(window_, rect(40, static_cast<float>(size.y) - 56, static_cast<float>(size.x) - 80, 34),
              kPanelSoft, messageError_ ? kDanger : kOk);
    drawText(window_, font_, clip(message_, 120), 54, static_cast<float>(size.y) - 48, 13,
             messageError_ ? kDanger : kOk);
  } else {
    drawText(window_, font_, "Aguardando o \"Jogar\" no site do Loomia (link loomia://)...", 40,
             static_cast<float>(size.y) - 48, 13, kMuted);
  }

  // ---- clique (usa as áreas do frame que acabamos de desenhar) ----------
  if (hasClick_) {
    hasClick_ = false;
    processClick(pendingClick_);
  }

  // ---- 3D por último: com scissor, o clear limpa SÓ a área de cada card
  // (apagando o painel 2D ali) e o boneco fica por cima da UI.
  flushPreviews();

  window_.display();
  return LauncherResult::None;
}

void Launcher::processClick(const sf::Vector2f& point) {
  for (const FieldHit& field : fieldRects_) {
    if (field.rect.contains(point)) {
      focused_ = field.id;
      return;
    }
  }

  for (auto it = buttonRects_.rbegin(); it != buttonRects_.rend(); ++it) {
    if (!it->enabled || !it->rect.contains(point)) continue;
    const std::string action = it->id;
    if (action == "submit") {
      if (registerMode_) doRegister(); else doLogin();
    } else if (action == "toggle") {
      registerMode_ = !registerMode_;
      focused_.clear();
    } else if (action == "logout") {
      profile_.logout();
      profile_.save();
      http_.setSessionToken("");
      tab_ = LauncherTab::Account;
      message_ = "você saiu da conta";
      messageError_ = false;
    } else if (action == "new") {
      startEdit(nullptr);
    } else if (action == "save") {
      saveAvatar();
    } else if (action == "cancel") {
      editing_ = false;
      tab_ = LauncherTab::Profile;
    } else if (action == "tab:0") {
      tab_ = LauncherTab::Account;
    } else if (action == "tab:1") {
      tab_ = LauncherTab::Profile;
    } else if (action == "tab:2") {
      if (!editing_) startEdit(profile_.activeAvatar());
      tab_ = LauncherTab::Avatar;
    } else if (action.rfind("activate:", 0) == 0) {
      const std::string id = action.substr(9);
      std::string error;
      if (api_.activateAvatar(id, error)) {
        profile_.setActive(id);
        profile_.save();
        message_ = "avatar ativado";
        messageError_ = false;
      } else {
        message_ = error;
        messageError_ = true;
      }
    } else if (action.rfind("edit:", 0) == 0) {
      if (const AvatarInfo* avatar = profile_.avatarById(action.substr(5))) startEdit(avatar);
    } else if (action.rfind("body:", 0) == 0) {
      bodyIndex_ = std::atoi(action.c_str() + 5);
    } else if (action.rfind("shirt:", 0) == 0) {
      shirtIndex_ = std::atoi(action.c_str() + 6);
    } else if (action.rfind("pants:", 0) == 0) {
      pantsIndex_ = std::atoi(action.c_str() + 6);
    } else if (action.rfind("hat:", 0) == 0) {
      hatIndex_ = std::atoi(action.c_str() + 4);
    }
    return;
  }
  focused_.clear();
}

}  // namespace loomia
