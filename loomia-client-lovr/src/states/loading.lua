-- Carregamento: baixa o mapa e abre a sala.
--
-- O trabalho de rede (`curl`, `io.popen`) é bloqueante, então cada `update`
-- avança UM passo: o frame anterior já desenhou a barra de progresso e o
-- usuário vê que o cliente está trabalhando (em vez de congelado).
--
-- Qualquer falha (sem rede, token inválido, mapa corrompido) mostra o motivo
-- e permite voltar ao launcher com Esc.

local loading = {}
loading.__index = loading

local world = require('src.world')
local net = require('src.net')
local config = require('src.config')

function loading.new(app)
  local self = setmetatable({}, loading)
  self.app = app
  self.title = 'Carregando Loomia…'
  self.detail = ''
  self.progress = 0
  self.failed = nil
  self.started = false
  self.stage = 0
  return self
end

function loading:start(args)
  self.args = args
  self.started = false
  self.stage = 0
  self.progress = 0
  self.failed = nil
  self.detail = ''
  self.transfer = nil
  self.welcome = nil
  self.welcomeError = nil
  if self.net then
    pcall(function() self.net:close() end)
    self.net = nil
  end
end

function loading:fail(message)
  self.failed = tostring(message)
  self.detail = ''
  if self.net then
    pcall(function() self.net:close() end)
    self.net = nil
  end
end

function loading:update(dt)
  if self.failed then return end
  local args = self.args
  if not args then
    self:fail('nenhum destino informado')
    return
  end

  if not self.started then
    self.started = true
    args.origin = ((args.origin or config.data.origin or ''):gsub('/+$', ''))
    if args.origin == '' then args.origin = config.data.origin end
    -- Sem host/porta o link está incompleto: melhor falhar aqui do que
    -- abrir um socket para o nada.
    if not args.host or not args.port then
      self:fail('o link não indicou o servidor da sala (host:porta)')
      return
    end
  end

  -- 1) Resolver o id do mapa.
  if self.stage == 0 then
    self.stage = 1
    self.detail = 'Procurando o mundo…'
    self.progress = 0.15
    if not args.map or args.map == '' then
      local http = require('src.http')
      http.origin = args.origin
      http.token = config.data.token
      local status, body = http.maps('')
      if status == 200 and type(body) == 'table' and body[1] then
        args.map = body[1].id
      else
        self:fail('o link não indicou o mapa e o catálogo não respondeu')
      end
    end
    return
  end

  -- 2) Baixar o mapa do master.
  if self.stage == 1 then
    self.stage = 2
    self.detail = ('Baixando o mundo "%s"…'):format(tostring(args.map))
    self.progress = 0.35
    local map, errorMessage = world.fetch(args.origin, args.map, config.data.token)
    if not map then
      self:fail(errorMessage or 'falha ao baixar o mapa')
      return
    end
    self.map = map
    return
  end

  -- 3) Montar os blocos (modelos 3D).
  if self.stage == 2 then
    self.stage = 3
    self.detail = 'Montando os blocos…'
    self.progress = 0.6
    local ok, built = pcall(world.new, self.map)
    if not ok then
      self:fail('o mapa recebido está inválido (' .. tostring(built) .. ')')
      return
    end
    self.world = built
    return
  end

  -- 4) Abrir a conexão UDP com a sala e enviar o hello.
  if self.stage == 3 then
    self.stage = 4
    self.detail = ('Conectando na sala %s…'):format(tostring(args.server))
    self.progress = 0.8
    local channel = net.new(args.host, args.port)
    local appearance = args.appearance or config.appearanceOf(args.avatar or config.activeAvatar())
    channel:hello(args.token, args.name or appearance.username, appearance)
    local ok, errorMessage = channel:connect()
    if not ok then
      self:fail(errorMessage or 'não foi possível abrir a conexão UDP')
      return
    end
    self.net = channel
    local owner = self
    channel.onMessage.welcome = function(message) owner.welcome = message end
    channel.onMessage.error = function(message) owner.welcomeError = message.message or 'erro da sala' end
    self.connectAt = lovr.timer.getTime()
    return
  end

  -- 5) Esperar o `welcome`.
  if self.stage == 4 then
    self.detail = 'Validando o acesso…'
    self.progress = 0.9
    if self.net then self.net:poll() end
    if self.welcomeError then
      self:fail(self.welcomeError)
      return
    end
    if self.welcome then
      self.stage = 5
      self.progress = 1
      self.detail = 'Entrando no mundo…'
      self.transfer = {
        world = self.world,
        net = self.net,
        welcome = self.welcome,
        args = args,
        appearance = args.appearance or config.appearanceOf(args.avatar or config.activeAvatar()),
      }
      return
    end
    if lovr.timer.getTime() - (self.connectAt or 0) > 12 then
      self:fail('a sala não respondeu (tempo esgotado)')
    end
  end
end

function loading:draw(pass)
  local width, height = lovr.graphics.getDimensions()
  pass:setColor(0.055, 0.082, 0.137, 1)
  pass:clear()

  local font = lovr.graphics.getDefaultFont()
  local function text(value, x, y, size, r, g, b, center)
    font:setPixelSize(size)
    pass:setFont(font)
    pass:setColor(r, g, b, 1)
    if center then x = x - font:getWidth(value, size) / 2 end
    pass:text(value, x, y, 0, 0, math.pi)
  end

  text('LOOMIA', width / 2, height - 92, 15, 0.918, 0.745, 0.098, true)
  text(self.title, width / 2, height / 2 + 46, 26, 0.93, 0.95, 0.99, true)
  text(self.detail or '', width / 2, height / 2 + 12, 15, 0.6, 0.66, 0.76, true)

  local barWidth = 360
  local barX = (width - barWidth) / 2
  local barY = height / 2 - 24
  local function rect(x, y, w, h, color)
    pass:setColor(color[1], color[2], color[3], 1)
    pass:triangle(x, y, 0, x + w, y, 0, x, y + h, 0)
    pass:triangle(x + w, y, 0, x + w, y + h, 0, x, y + h, 0)
  end
  rect(barX, barY, barWidth, 8, { 0.12, 0.16, 0.25 })
  local filled = barWidth * math.max(0, math.min(1, self.progress))
  rect(barX, barY, filled, 8, { 0.29, 0.66, 1 })

  if self.failed then
    text('Não foi possível entrar: ' .. tostring(self.failed), width / 2, height / 2 - 70, 15, 1, 0.45, 0.45, true)
    text('Esc — voltar ao cliente', width / 2, height / 2 - 100, 13, 0.65, 0.7, 0.8, true)
  end
end

function loading:keypressed(key)
  if key == 'escape' then
    if self.net then
      pcall(function() self.net:close() end)
      self.net = nil
    end
    self.app.state = 'launcher'
    if self.app.launcher then self.app.launcher:enter() end
  end
end

return loading
