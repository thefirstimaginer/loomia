-- Launcher: a "cara" do cliente. Funciona SEM servidor.
--
-- Abas:
--   Jogar    — catálogo de mundos, busca e "Entrar no mundo"
--   Perfil   — dados da conta + avatares salvos
--   Avatar   — editor embutido (pele, camisa, calça, chapéu) com salvar
--   Conta    — login / criação de conta
--
-- O servidor só é necessário para: conectar (status/envio), login, catálogo e
-- baixar o mapa. Sem ele a tela continua abrindo, com "offline" e cache.

local launcher = {}
launcher.__index = launcher

local http = require('src.http')
local config = require('src.config')
local ui = require('src.ui')

local HATS = {
  { id = '', label = 'Nenhum' },
  { id = 'cap', label = 'Boné' },
  { id = 'top_hat', label = 'Cartola' },
  { id = 'propeller', label = 'Hélice' },
  { id = 'crown', label = 'Coroa' },
}

local BODY = { '#eabe19', '#e0af8c', '#8c5a3c', '#f5d2aa', '#78c88c', '#bebec8' }
local SHIRT = { '#0f78be', '#be2828', '#e6e6e6', '#464646', '#f0a014', '#6e3ca0' }
local PANTS = { '#234b2d', '#1e1e28', '#5a4632', '#3c3c78', '#787878' }

local function hexToRgb(hex)
  hex = (hex or '#000'):gsub('#', '')
  return {
    tonumber(hex:sub(1, 2), 16) or 0,
    tonumber(hex:sub(3, 4), 16) or 0,
    tonumber(hex:sub(5, 6), 16) or 0,
  }
end

local function rgbToHex(color)
  color = color or { 0, 0, 0 }
  return ('#%02x%02x%02x'):format(color[1] or 0, color[2] or 0, color[3] or 0)
end

local function clampText(text, max)
  text = tostring(text or '')
  if #text <= max then return text end
  return text:sub(1, max - 1) .. '…'
end

function launcher.new(app)
  local self = setmetatable({}, launcher)
  self.app = app
  self.ui = ui.new()
  self.tab = 'play'
  self.fields = {
    login = '', password = '',
    firstName = '', lastName = '', nickname = '', email = '', passwordConfirm = '',
  }
  self.focus = nil
  self.message = nil      -- { kind = 'ok'|'error'|'info', text = }
  self.maps = config.data.mapsCache or {}
  self.search = ''
  self.selectedMap = nil
  self.busy = false
  self.busyLabel = ''
  self.serverStatus = nil -- { online = bool, players = n, rooms = n }
  self.registerMode = false
  self.reconnectAt = 0
  self.editor = nil
  self.connection = { host = '', port = '', token = '', name = '' }
  self.lastHealthAt = 0
  return self
end

-- ---------------------------------------------------------------------------
-- Rede (tudo bloqueante, chamado apenas em ação do usuário)
-- ---------------------------------------------------------------------------

function launcher:setBusy(label)
  self.busy = true
  self.busyLabel = label or 'Aguarde…'
end

function launcher:clearBusy()
  self.busy = false
  self.busyLabel = ''
end

function launcher:notify(kind, text)
  self.message = { kind = kind, text = text }
end

function launcher:checkServer(silent)
  http.origin = config.data.origin
  http.token = config.data.token
  local status, body = http.health()
  local online = status == 200 and body.ok == true
  config.data.offline = not online
  if online then
    local statusCode, info = http.request('GET', '/api/status')
    if statusCode == 200 then
      self.serverStatus = {
        online = true,
        players = info.players_online or 0,
        rooms = #(info.rooms or {}),
        version = info.version,
      }
    else
      self.serverStatus = { online = true }
    end
    if not silent then
      self:notify('ok', 'Servidor conectado.')
    end
  else
    self.serverStatus = { online = false, reason = body.error or ('HTTP ' .. tostring(status)) }
    if not silent then
      self:refreshFromCache()
      self:notify('error', 'Servidor offline: você pode editar seus avatares, mas entrar nos mundos precisa dele.')
    end
  end
  return online
end

function launcher:refreshMaps()
  local status, body = http.maps(self.search)
  if status == 200 and type(body) == 'table' then
    self.maps = body
    config.data.mapsCache = body
    config.save()
  end
end

function launcher:refreshSession()
  if not config.data.token then return end
  local status, body = http.session()
  if status == 200 and body.user then
    config.data.user = body.user
    config.data.avatars = body.avatars or {}
    config.save()
  elseif status == 401 then
    config.logout()
  end
end

function launcher:refreshFromCache()
  self.maps = config.data.mapsCache or self.maps
end

-- ---------------------------------------------------------------------------
-- Ações
-- ---------------------------------------------------------------------------

function launcher:doLogin()
  self:setBusy('Entrando…')
  http.origin = config.data.origin
  local ok, body = http.login(self.fields.login, self.fields.password)
  self:clearBusy()
  if not ok then
    self:notify('error', body.error or 'Não foi possível entrar.')
    return
  end
  config.data.token = http.token
  config.data.user = body.user
  config.data.avatars = body.avatars or {}
  config.save()
  self.fields.password = ''
  self.tab = (body.hasAvatar and 'play') or 'avatar'
  if not body.hasAvatar then
    self:startEditor(nil)
    self:notify('info', 'Bem-vindo! Monte seu primeiro avatar.')
  else
    self:notify('ok', 'Olá, ' .. (body.user.nickname or body.user.firstName) .. '!')
  end
  self:refreshMaps()
end

function launcher:doRegister()
  self:setBusy('Criando conta…')
  http.origin = config.data.origin
  local ok, body = http.register({
    firstName = self.fields.firstName,
    lastName = self.fields.lastName,
    nickname = self.fields.nickname,
    email = self.fields.email,
    password = self.fields.password,
    passwordConfirm = self.fields.passwordConfirm,
  })
  self:clearBusy()
  if not ok then
    self:notify('error', body.error or 'Não foi possível criar a conta.')
    return
  end
  config.data.token = http.token
  config.data.user = body.user
  config.data.avatars = {}
  config.save()
  self.registerMode = false
  self.tab = 'avatar'
  self:startEditor(nil)
  self:notify('ok', 'Conta criada! Agora monte seu avatar.')
end

function launcher:doLogout()
  http.origin = config.data.origin
  http.token = config.data.token
  pcall(http.request, 'POST', '/api/client/logout')
  config.logout()
  http.token = nil
  self.tab = 'account'
  self:notify('info', 'Você saiu da conta.')
end

function launcher:startEditor(avatar)
  local active = avatar or config.activeAvatar()
  self.editor = {
    id = active and active.id or nil,
    name = active and active.name or ('Avatar de ' .. clampText(self.fields.nickname ~= '' and self.fields.nickname or (config.data.user and config.data.user.nickname) or 'jogador', 20)),
    body = rgbToHex(active and active.bodyColor or hexToRgb('#eabe19')),
    shirt = rgbToHex(active and active.shirtColor or hexToRgb('#0f78be')),
    pants = rgbToHex(active and active.pantsColor or hexToRgb('#234b2d')),
    hat = (active and active.hat) or '',
  }
  self.tab = 'avatar'
end

function launcher:saveEditor()
  local editor = self.editor
  if not editor then return end
  self:setBusy('Salvando avatar…')
  http.origin = config.data.origin
  local payload = {
    name = editor.name ~= '' and editor.name or 'Meu avatar',
    bodyColor = hexToRgb(editor.body),
    shirtColor = hexToRgb(editor.shirt),
    pantsColor = hexToRgb(editor.pants),
    hat = editor.hat ~= '' and editor.hat or nil,
    isActive = true,
  }
  local status, body = http.saveAvatar(editor.id, payload)
  self:clearBusy()
  if status ~= 200 and status ~= 201 then
    self:notify('error', body.error or 'Não foi possível salvar o avatar.')
    return
  end
  self:refreshSession()
  self:startEditor(nil)
  self:notify('ok', 'Avatar salvo.')
end

function launcher:activateAvatar(id)
  self:setBusy('Ativando…')
  http.origin = config.data.origin
  local status, body = http.activateAvatar(id)
  self:clearBusy()
  if status ~= 200 then
    self:notify('error', body.error or 'Falha ao ativar o avatar.')
    return
  end
  config.data.activeAvatarId = id
  self:refreshSession()
end

function launcher:enterMap(map)
  if not map then return end
  if not config.data.token then
    self.tab = 'account'
    self:notify('error', 'Entre na sua conta para jogar.')
    return
  end
  self:setBusy('Abrindo sala de "' .. map.name .. '"…')
  http.origin = config.data.origin
  local avatar = config.activeAvatar()
  local status, body = http.launch(map.id, avatar and avatar.id or nil)
  self:clearBusy()
  if status ~= 200 then
    self:notify('error', body.error or 'Não foi possível abrir a sala.')
    return
  end
  self:notify('ok', body.message or 'Sala pronta')
  self.app.loading:start({
    server = body.server,
    host = body.server:match('^([^:]+)'),
    port = tonumber(body.server:match(':(%d+)$')),
    token = body.token,
    map = body.map_id or map.id,
    name = (config.data.user and config.data.user.nickname) or 'Loomian',
    origin = config.data.origin,
    hover = true,
    appearance = config.appearanceOf(body.avatar or avatar),
    avatar = body.avatar or avatar,
  })
  self.app.state = 'loading'
end

function launcher:connectDirect()
  local host = self.connection.host
  local port = tonumber(self.connection.port)
  if not host or not port then
    self:notify('error', 'Informe host e porta da sala.')
    return
  end
  self.app.loading:start({
    server = ('%s:%d'):format(host, port),
    host = host,
    port = port,
    token = self.connection.token,
    name = self.connection.name ~= '' and self.connection.name or 'Loomian',
    origin = config.data.origin,
    appearance = config.appearanceOf(config.activeAvatar()),
  })
  self.app.state = 'loading'
end

-- ---------------------------------------------------------------------------
-- Loop
-- ---------------------------------------------------------------------------

-- Trabalho de rede pendente: só roda no começo do frame, depois de a UI ter
-- sido desenhada pelo menos uma vez — assim o botão responde e o "Aguarde…"
-- aparece antes do `curl` bloquear.
launcher.pendingJob = nil

function launcher:runLater(job)
  self.pendingJob = job
end

function launcher:update(dt)
  self.clock = (self.clock or 0) + dt

  if self.pendingJob then
    local job = self.pendingJob
    self.pendingJob = nil
    self.framesAfterJob = 0
    local ok, errorMessage = pcall(job)
    if not ok then
      self:clearBusy()
      self:notify('error', 'Falha de rede: ' .. tostring(errorMessage))
    end
    return
  end

  -- Verifica o servidor ao abrir e a cada 15 s.
  if (self.clock or 0) - self.lastHealthAt > 15 or self.lastHealthAt == 0 then
    self.lastHealthAt = self.clock
    if not self.busy then
      self:runLater(function() self:checkServer(true) end)
    end
  end
end

-- ---------------------------------------------------------------------------
-- Desenho
-- ---------------------------------------------------------------------------

function launcher:drawAvatar(pass, x, y, size, look)
  -- Prévia 3D simplificada: cabeça, torso e pernas como retângulos.
  local body = look.body_color or look.bodyColor or { 234, 190, 25 }
  local shirt = look.shirt_color or look.shirtColor or { 15, 120, 190 }
  local pants = look.pants_color or look.pantsColor or { 35, 75, 45 }

  local function box(px, py, w, h, color)
    pass:setColor(color[1] / 255, color[2] / 255, color[3] / 255, 1)
    pass:triangle(px, py, 0, px + w, py, 0, px, py + h, 0)
    pass:triangle(px + w, py, 0, px + w, py + h, 0, px, py + h, 0)
  end

  local unit = size / 10
  box(x + unit * 3, y + unit * 7.4, unit * 4, unit * 2.6, body)       -- cabeça
  box(x + unit * 2.6, y + unit * 3.4, unit * 4.8, unit * 4, shirt)    -- torso
  box(x + unit * 3.4, y + unit * 0.6, unit * 1.4, unit * 2.8, pants)  -- perna
  box(x + unit * 5.2, y + unit * 0.6, unit * 1.4, unit * 2.8, pants)  -- perna

  local hat = look.hat
  if hat == 'cap' then box(x + unit * 2.9, y + unit * 9.4, unit * 4.2, unit * 0.9, { 200, 45, 45 })
  elseif hat == 'top_hat' then
    box(x + unit * 2.6, y + unit * 9.3, unit * 4.8, unit * 0.4, { 20, 20, 26 })
    box(x + unit * 3.4, y + unit * 9.7, unit * 3.2, unit * 2.2, { 20, 20, 26 })
  elseif hat == 'crown' then box(x + unit * 3, y + unit * 9.4, unit * 4, unit * 0.9, { 242, 199, 38 })
  elseif hat == 'propeller' then
    box(x + unit * 2.9, y + unit * 9.3, unit * 4.2, unit * 0.8, { 250, 200, 40 })
    box(x + unit * 2.2, y + unit * 10.1, unit * 5.6, unit * 0.35, { 60, 150, 220 })
  end
end

function launcher:draw(pass)
  local width, height = self.ui:size()
  local u = self.ui
  u:beginFrame()

  -- Fundo.
  u:rect(pass, 0, 0, width, height, 'bg')

  -- ------------------------------------------------------------------ cabeçalho
  local barHeight = 64
  u:rect(pass, 0, height - barHeight, width, barHeight, 'panel')
  u:text(pass, 'LOOMIA', 26, height - 42, 26, 'gold')
  u:text(pass, 'cliente · launcher', 128, height - 40, 13, 'muted')

  -- Status do servidor
  local online = self.serverStatus and self.serverStatus.online
  local dot = online and 'ok' or 'danger'
  u:rect(pass, width - 300, height - 40, 10, 10, dot)
  local label = online and ('online · ' .. tostring((self.serverStatus and self.serverStatus.players) or 0) .. ' jogando')
    or 'offline'
  u:text(pass, label, width - 284, height - 43, 13, online and 'ok' or 'danger')
  u:button(pass, {
    x = width - 150, y = height - 46, width = 120, height = 30, label = 'Conectar servidor',
    size = 12,
    onClick = function()
      self:refreshFromCache()
      self:runLater(function() self:checkServer(false) end)
    end,
  })

  -- Abas
  local tabs = {
    { 'play', 'Jogar' },
    { 'profile', 'Perfil' },
    { 'avatar', 'Avatar' },
    { 'account', 'Conta' },
  }
  local tabX = 330
  for _, entry in ipairs(tabs) do
    local id, title = entry[1], entry[2]
    local active = self.tab == id
    local labelWidth = #title * 9 + 26
    u:button(pass, {
      x = tabX, y = height - 46, width = labelWidth, height = 30, label = title, size = 13,
      variant = active and 'accent' or nil,
      onClick = function()
        self.tab = id
        if id == 'avatar' and not self.editor and config.data.token then self:startEditor(nil) end
        if id == 'play' and online then self:refreshMaps() end
      end,
    })
    tabX = tabX + labelWidth + 8
  end

  -- ------------------------------------------------------------------ corpo
  local bodyY = 24
  local bodyHeight = height - barHeight - 48

  if not config.data.token then
    self:drawAccount(pass, 26, bodyY, width - 52, bodyHeight)
  elseif self.tab == 'play' then
    self:drawPlay(pass, 26, bodyY, width - 52, bodyHeight, online)
  elseif self.tab == 'profile' then
    self:drawProfile(pass, 26, bodyY, width - 52, bodyHeight)
  elseif self.tab == 'avatar' then
    self:drawEditor(pass, 26, bodyY, width - 52, bodyHeight)
  else
    self:drawAccount(pass, 26, bodyY, width - 52, bodyHeight)
  end

  -- Aviso (mensagem)
  if self.message then
    local color = self.message.kind == 'error' and 'danger' or (self.message.kind == 'ok' and 'ok' or 'accent')
    u:rect(pass, 26, 10, width - 52, 30, 'panelSoft')
    u:rect(pass, 26, 10, 4, 30, color)
    u:text(pass, clampText(self.message.text, 160), 40, 18, 13, color)
  end

  if self.busy then
    u:rect(pass, width / 2 - 140, height / 2 - 24, 280, 48, 'panel')
    u:text(pass, self.busyLabel, width / 2, height / 2 - 8, 15, 'text', 1, true)
  end
end

-- ---------------------------------------------------------------------------
-- Aba: conta (login / registro)
-- ---------------------------------------------------------------------------

function launcher:drawAccount(pass, x, y, width, height)
  local u = self.ui
  local cardWidth = math.min(460, width)
  local cardX = x + (width - cardWidth) / 2
  local top = y + height
  u:card(pass, cardX, top - 430, cardWidth, 410)

  u:text(pass, self.registerMode and 'Criar conta' or 'Entrar na sua conta', cardX + 24, top - 60, 20, 'text')
  u:text(pass, 'O cliente precisa da conta para entrar nos mundos.', cardX + 24, top - 84, 12, 'muted')

  local fieldY = top - 130
  local function field(key, placeholder, secret, label)
    u:text(pass, label, cardX + 24, fieldY + 40, 11, 'muted')
    u:field(pass, {
      x = cardX + 24, y = fieldY, width = cardWidth - 48, height = 34,
      value = secret and string.rep('•', #self.fields[key]) or self.fields[key],
      placeholder = placeholder,
      active = self.focus == key,
      onClick = function() self.focus = key end,
    })
    fieldY = fieldY - 56
  end

  if self.registerMode then
    field('firstName', 'Ana', false, 'NOME')
    field('lastName', 'Lima', false, 'SOBRENOME')
    field('nickname', 'ana_lima', false, 'APELIDO')
    field('email', 'ana@loomia.dev', false, 'EMAIL')
    field('password', 'mínimo 8 caracteres', true, 'SENHA')
    field('passwordConfirm', 'repita a senha', true, 'REPETIR SENHA')
  else
    field('login', 'email ou apelido', false, 'EMAIL OU APELIDO')
    field('password', 'sua senha', true, 'SENHA')
  end

  u:button(pass, {
    x = cardX + 24, y = fieldY + 6, width = cardWidth - 48, height = 38,
    label = self.registerMode and 'CRIAR CONTA' or 'ENTRAR',
    variant = 'accent', size = 14,
    onClick = function()
      if self.registerMode then self:runLater(function() self:doRegister() end)
      else self:runLater(function() self:doLogin() end) end
    end,
  })
  u:button(pass, {
    x = cardX + 24, y = fieldY - 40, width = cardWidth - 48, height = 30,
    label = self.registerMode and 'Já tenho conta' or 'Criar uma conta nova', size = 12,
    onClick = function() self.registerMode = not self.registerMode end,
  })

  -- Conexão direta (sem catálogo): host, porta e token da sala.
  u:text(pass, 'Ou conecte direto em uma sala', cardX + 24, y + 118, 12, 'muted')
  u:field(pass, {
    x = cardX + 24, y = y + 72, width = (cardWidth - 60) / 2, height = 30,
    value = self.connection.host, placeholder = 'host', active = self.focus == 'connHost',
    onClick = function() self.focus = 'connHost' end,
  })
  u:field(pass, {
    x = cardX + 36 + (cardWidth - 60) / 2, y = y + 72, width = (cardWidth - 60) / 2, height = 30,
    value = self.connection.port, placeholder = 'porta', active = self.focus == 'connPort',
    onClick = function() self.focus = 'connPort' end,
  })
  u:field(pass, {
    x = cardX + 24, y = y + 34, width = cardWidth - 150, height = 30,
    value = self.connection.token, placeholder = 'token da sala', active = self.focus == 'connToken',
    onClick = function() self.focus = 'connToken' end,
  })
  u:button(pass, {
    x = cardX + cardWidth - 118, y = y + 34, width = 94, height = 30, label = 'CONECTAR',
    onClick = function() self:runLater(function() self:connectDirect() end) end,
  })
end

-- ---------------------------------------------------------------------------
-- Aba: jogar (catálogo)
-- ---------------------------------------------------------------------------

function launcher:drawPlay(pass, x, y, width, height, online)
  local u = self.ui
  local top = y + height

  u:text(pass, 'Explorar mundos', x, top - 20, 20, 'text')
  u:text(pass, online and 'Escolha um mundo e entre na sala.' or 'Servidor offline: catálogo em cache.',
    x, top - 42, 12, online and 'muted' or 'warn')

  u:field(pass, {
    x = x + width - 430, y = top - 70, width = 300, height = 34,
    value = self.search, placeholder = 'Pesquisar mundos…', active = self.focus == 'search',
    onClick = function() self.focus = 'search' end,
  })
  u:button(pass, {
    x = x + width - 120, y = top - 70, width = 110, height = 34, label = 'ATUALIZAR', size = 12,
    onClick = function()
      self:runLater(function()
        if self:checkServer(true) then self:refreshMaps() end
      end)
    end,
  })

  -- Lista de mundos (duas colunas) e painel de detalhe.
  local listWidth = width * 0.58
  local cardsTop = top - 92
  local cardHeight = 66
  local shown = 0
  for _, map in ipairs(self.maps or {}) do
    if shown >= 7 then break end
    local cardY = cardsTop - (shown + 1) * (cardHeight + 8)
    local selected = self.selectedMap == map.id
    u:card(pass, x, cardY, listWidth, cardHeight, selected and 'panelSoft' or 'panel')
    u:text(pass, clampText(map.name, 30), x + 14, cardY + 40, 15, 'text')
    u:text(pass, clampText(map.author or '', 24) .. ' · ' .. tostring(map.blocks or 0) .. ' blocos',
      x + 14, cardY + 20, 11, 'muted')
    if map.online and map.online > 0 then
      u:rect(pass, x + listWidth - 90, cardY + 38, 8, 8, 'ok')
      u:text(pass, tostring(map.online) .. ' online', x + listWidth - 76, cardY + 35, 11, 'ok')
    end
    local mapId = map.id
    u.hits[#u.hits + 1] = {
      x = x, y = cardY, width = listWidth, height = cardHeight,
      onClick = function() self.selectedMap = mapId end,
    }
    shown = shown + 1
  end

  if shown == 0 then
    u:card(pass, x, cardsTop - 120, listWidth, 90, 'panel')
    u:text(pass, online and 'Nenhum mundo publicado ainda.' or 'Sem conexão para carregar o catálogo.',
      x + 16, cardsTop - 70, 14, 'muted')
  end

  -- Detalhe
  local detailX = x + listWidth + 18
  local detailWidth = width - listWidth - 18
  u:card(pass, detailX, y, detailWidth, height, 'panel')

  local selected
  for _, map in ipairs(self.maps or {}) do
    if map.id == self.selectedMap then selected = map end
  end
  selected = selected or (self.maps or {})[1]

  if selected then
    u:text(pass, clampText(selected.name, 28), detailX + 20, y + height - 46, 18, 'text')
    u:text(pass, clampText(selected.description or 'Sem descrição.', 90), detailX + 20, y + height - 76, 12, 'muted')
    self:drawAvatar(pass, detailX + 30, y + height - 300, 170, config.appearanceOf(config.activeAvatar()))
    u:text(pass, config.activeAvatar() and config.activeAvatar().name or 'Avatar padrão',
      detailX + 30, y + height - 328, 12, 'muted')

    u:button(pass, {
      x = detailX + 20, y = y + 70, width = detailWidth - 40, height = 44,
      label = 'ENTRAR NO MUNDO', variant = 'accent', size = 15,
      disabled = not config.data.token,
      onClick = function() self:runLater(function() self:enterMap(selected) end) end,
    })
    u:button(pass, {
      x = detailX + 20, y = y + 30, width = detailWidth - 40, height = 30,
      label = 'Editar meu avatar', size = 12,
      onClick = function() self:startEditor(nil) end,
    })
  end
end

-- ---------------------------------------------------------------------------
-- Aba: perfil
-- ---------------------------------------------------------------------------

function launcher:drawProfile(pass, x, y, width, height)
  local u = self.ui
  local user = config.data.user or {}
  local top = y + height

  u:card(pass, x, top - 120, width, 100, 'panelSoft')
  local avatar = config.activeAvatar()
  self:drawAvatar(pass, x + 18, top - 112, 84, config.appearanceOf(avatar))
  u:text(pass, (user.nickname or 'jogador'), x + 118, top - 56, 22, 'text')
  u:text(pass, ('%s %s · %s'):format(user.firstName or '', user.lastName or '', user.email or ''),
    x + 118, top - 80, 12, 'muted')
  u:text(pass, ('Conta criada em %s'):format((user.createdAt or ''):sub(1, 10)), x + 118, top - 100, 11, 'muted')
  u:button(pass, {
    x = x + width - 130, y = top - 66, width = 110, height = 30, label = 'SAIR', size = 12,
    onClick = function() self:runLater(function() self:doLogout() end) end,
  })

  u:text(pass, 'Seus avatares', x, top - 148, 16, 'text')
  u:text(pass, 'Clique em um avatar para ativá-lo; "Editar" abre o editor.', x, top - 166, 11, 'muted')

  local cardsY = top - 200
  local cardWidth = 190
  local index = 0
  for _, entry in ipairs(config.data.avatars or {}) do
    local column = index % 4
    local row = math.floor(index / 4)
    local cx = x + column * (cardWidth + 14)
    local cy = cardsY - row * 230
    u:card(pass, cx, cy - 200, cardWidth, 200, 'panel')
    if entry.isActive then
      u:rect(pass, cx, cy - 200, cardWidth, 3, 'ok')
    end
    self:drawAvatar(pass, cx + 45, cy - 150, 100, config.appearanceOf(entry))
    u:text(pass, clampText(entry.name, 18), cx + 14, cy - 172, 13, 'text')
    local avatarId = entry.id
    u:button(pass, {
      x = cx + 10, y = cy - 38, width = 84, height = 26, label = entry.isActive and 'ATIVO' or 'USAR', size = 11,
      variant = entry.isActive and 'accent' or nil,
      onClick = function() self:runLater(function() self:activateAvatar(avatarId) end) end,
    })
    u:button(pass, {
      x = cx + 100, y = cy - 38, width = 80, height = 26, label = 'EDITAR', size = 11,
      onClick = function() self:startEditor(entry) end,
    })
    index = index + 1
  end

  if index == 0 then
    u:card(pass, x, cardsY - 120, width, 100, 'panel')
    u:text(pass, 'Nenhum avatar ainda.', x + 16, cardsY - 70, 14, 'muted')
    u:button(pass, {
      x = x + 16, y = cardsY - 106, width = 180, height = 30, label = 'CRIAR AVATAR',
      variant = 'accent',
      onClick = function() self:startEditor(nil) end,
    })
  end
end

-- ---------------------------------------------------------------------------
-- Aba: editor de avatar
-- ---------------------------------------------------------------------------

function launcher:drawEditor(pass, x, y, width, height)
  local u = self.ui
  local editor = self.editor
  if not editor then
    u:text(pass, 'Escolha "Novo avatar" no perfil.', x, y + height - 40, 14, 'muted')
    return
  end
  local top = y + height
  local previewWidth = 260

  u:card(pass, x, y, previewWidth, height, 'panelSoft')
  self:drawAvatar(pass, x + 60, top - 250, 140, {
    body_color = hexToRgb(editor.body),
    shirt_color = hexToRgb(editor.shirt),
    pants_color = hexToRgb(editor.pants),
    hat = editor.hat ~= '' and editor.hat or nil,
  })
  u:field(pass, {
    x = x + 16, y = y + 20, width = previewWidth - 32, height = 32,
    value = editor.name, placeholder = 'Nome do avatar', active = self.focus == 'avatarName',
    onClick = function() self.focus = 'avatarName' end,
  })

  local formX = x + previewWidth + 24
  local formWidth = width - previewWidth - 24

  u:text(pass, editor.id and 'Editar avatar' or 'Novo avatar', formX, top - 24, 20, 'text')

  local rowY = top - 76
  u:text(pass, 'PELE', formX, rowY + 32, 11, 'muted')
  u:swatches(pass, { x = formX, y = rowY, colors = BODY, value = editor.body,
    onChange = function(hex) editor.body = hex end })

  rowY = rowY - 70
  u:text(pass, 'CAMISA', formX, rowY + 32, 11, 'muted')
  u:swatches(pass, { x = formX, y = rowY, colors = SHIRT, value = editor.shirt,
    onChange = function(hex) editor.shirt = hex end })

  rowY = rowY - 70
  u:text(pass, 'CALÇA', formX, rowY + 32, 11, 'muted')
  u:swatches(pass, { x = formX, y = rowY, colors = PANTS, value = editor.pants,
    onChange = function(hex) editor.pants = hex end })

  rowY = rowY - 66
  u:text(pass, 'CHAPÉU', formX, rowY + 38, 11, 'muted')
  u:chips(pass, { x = formX, y = rowY, items = HATS, value = editor.hat,
    onChange = function(hatId) editor.hat = hatId end })

  u:button(pass, {
    x = formX, y = y + 70, width = 210, height = 42, label = 'SALVAR AVATAR',
    variant = 'accent', size = 14,
    onClick = function() self:runLater(function() self:saveEditor() end) end,
  })
  u:button(pass, {
    x = formX + 222, y = y + 70, width = 150, height = 42, label = 'CANCELAR',
    onClick = function()
      self.editor = nil
      self.tab = config.data.avatars and #config.data.avatars > 0 and 'profile' or 'play'
    end,
  })
  u:button(pass, {
    x = formX, y = y + 20, width = 210, height = 32, label = 'Novo avatar do zero', size = 12,
    onClick = function()
      editor.id = nil
      editor.name = 'Novo avatar'
      editor.body = BODY[1]
      editor.shirt = SHIRT[1]
      editor.pants = PANTS[1]
      editor.hat = ''
    end,
  })
end

-- ---------------------------------------------------------------------------
-- Entrada
-- ---------------------------------------------------------------------------

function launcher:click(x, y)
  -- A UI desenha com origem no rodapé; o mouse vem com origem no topo.
  return self.ui:click(x, y)
end

-- O launcher responde no SOLTAR do botão esquerdo (clique de verdade).
function launcher:mousereleased(x, y, button)
  if button ~= 1 then return end
  -- `click` usa as áreas do ÚLTIMO frame desenhado (que é o que o usuário viu).
  local handled = self:click(x, y)
  -- Clicar longe de qualquer botão/campo tira o foco da digitação.
  if not handled then self.focus = nil end
end

function launcher:mousemoved(x, y, dx, dy)
  local hit = self.ui:pointer(x, y)
  -- Cursor de mão nos botões (feedback simples).
  if lovr.mouse then
    local hand = hit ~= nil
    if hand ~= self.handCursor then
      self.handCursor = hand
      pcall(function() lovr.mouse.setCursorSystem(hand and 'hand' or 'arrow') end)
    end
  end
end

function launcher:enter()
  -- Ao voltar do jogo/loading: recarrega perfil e catálogo do cache.
  self:refreshFromCache()
  self.busy = false
  self.busyLabel = ''
end

function launcher:textinput(text)
  if not self.focus then return end
  if self.focus == 'connPort' and not text:match('^%d+$') then return end
  local key = self.focus
  if key == 'search' then
    self.search = self.search .. text
    if #self.search <= 40 then self:refreshMaps() end
    return
  end
  local current = self.fields[key] or self.connection[key] or (self.editor and self.editor[key]) or ''
  if #current >= 120 then return end
  local value = current .. text
  if self.fields[key] ~= nil then self.fields[key] = value
  elseif self.connection[key] ~= nil then self.connection[key] = value
  elseif self.editor and self.editor[key] ~= nil then self.editor[key] = value
  end
end

function launcher:keypressed(key)
  if key == 'tab' then
    -- Alterna o foco entre os campos principais.
    local order = { 'login', 'password', 'firstName', 'lastName', 'nickname', 'email', 'passwordConfirm',
      'connHost', 'connPort', 'connToken', 'search', 'avatarName' }
    local index = 1
    for position, name in ipairs(order) do
      if name == self.focus then index = position + 1 end
    end
    self.focus = order[((index - 1) % #order) + 1]
    return
  end
  if key == 'backspace' and self.focus then
    local target = self.focus
    if target == 'search' then
      self.search = self.search:sub(1, -2)
      self:refreshMaps()
      return
    end
    if self.fields[target] ~= nil then
      self.fields[target] = self.fields[target]:sub(1, -2)
    elseif self.connection[target] ~= nil then
      self.connection[target] = self.connection[target]:sub(1, -2)
    elseif self.editor and self.editor[target] ~= nil then
      self.editor[target] = self.editor[target]:sub(1, -2)
    end
    return
  end
  if key == 'return' then
    if self.focus == 'login' or self.focus == 'password' then self:doLogin() end
    if self.focus == 'passwordConfirm' then self:doRegister() end
    return
  end
  if key == 'escape' then
    self.focus = nil
    return
  end
  if key == 'f5' then
    if self:checkServer(true) then self:refreshMaps() end
  end
end

function launcher:enter()
  -- Chamado ao voltar do jogo: recarrega o que mudou.
  self:refreshSession()
end

return launcher
