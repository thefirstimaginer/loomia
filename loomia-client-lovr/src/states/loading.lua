-- Estado de carregamento: busca o mapa no master e abre a sala (UDP).

local loading = {}
loading.__index = loading

local world = require('src.world')
local net = require('src.net')

function loading.new(app)
  local self = setmetatable({}, loading)
  self.app = app
  self.message = 'Carregando Loomia, aguarde!'
  self.detail = ''
  self.timer = 0
  self.started = false
  return self
end

function loading:start(args)
  self.args = args
  self.started = false
  self.done = nil
  self.failed = nil
end

function loading:update(dt)
  self.timer = self.timer + dt
  if self.started then return end
  self.started = true

  local args = self.args
  local mapId = args.map
  local origin = args.origin

  if not mapId and origin then
    -- O link pode não trazer o mapa: usa o primeiro do catálogo.
    self.detail = 'procurando o mapa no catálogo…'
    local handle = io.popen(('curl -fsSL "%s/api/maps" 2>/dev/null'):format(origin:gsub('/$', '')))
    if handle then
      local text = handle:read('*a')
      handle:close()
      local ok, list = pcall(lovr.data.decode, 'json', text)
      if not ok then ok, list = pcall(require('src.json').decode, text) end
      if ok and type(list) == 'table' and list[1] then mapId = list[1].id end
    end
  end

  if not mapId then
    self.failed = 'o link não indicou o mapa'
    return
  end

  self.detail = 'baixando o mapa "' .. mapId .. '"…'
  local map, err = world.fetch(origin or ('http://' .. args.host), mapId)
  if not map then
    self.failed = err or 'falha ao carregar o mapa'
    return
  end

  self.detail = 'montando o mundo…'
  local built = world.new(map)

  -- Abre a rede UDP (via processo auxiliar) e manda o `hello`.
  local channel = net.new(args.host, args.port)
  channel:hello(args.token, args.name, {
    username = args.name,
    body_color = self.appearance and self.appearance.body_color or { 234, 190, 25 },
    shirt_color = self.appearance and self.appearance.shirt_color or { 15, 120, 190 },
    pants_color = self.appearance and self.appearance.pants_color or { 35, 75, 45 },
    hat = args.hat,
  })
  local ok, error = channel:connect()
  if not ok then
    self.failed = error
    return
  end

  self.detail = 'entrando na sala ' .. args.server .. '…'
  self.world = built
  self.net = channel
  self.receivedWelcome = false

  -- Espera o `welcome` (a sala valida o token). Dá até 8 s.
  channel.onMessage.welcome = function(message)
    self.receivedWelcome = true
    self.welcome = message
    self.nextState = 'game'
    self.transfer = {
      world = built,
      net = channel,
      welcome = message,
      args = args,
      appearance = self.appearance,
    }
  end
  channel.onMessage.error = function(message)
    self.failed = message.message or 'erro da sala'
  end

  self.waitStarted = lovr.timer.getTime()
end

function loading:draw(pass)
  local width, height = lovr.graphics.getDimensions()
  pass:setColor(0.08, 0.12, 0.2, 1)
  pass:clear()
  local font = lovr.graphics.getDefaultFont()
  font:setPixelSize(32)
  pass:setFont(font)
  pass:setColor(1, 1, 1, 1)
  pass:text(self.message, width / 2, height / 2 + 40, 0.5, 0, 0, 0, 0.5)
  font:setPixelSize(18)
  pass:setFont(font)
  if self.failed then
    pass:setColor(1, 0.45, 0.45, 1)
    pass:text('Erro: ' .. self.failed, width / 2, height / 2, 0.5, 0, 0, 0, 0.5)
    pass:setColor(0.7, 0.75, 0.8, 1)
    pass:text('Aperte Esc para voltar ao menu', width / 2, height / 2 - 30, 0.5, 0, 0, 0, 0.5)
  else
    pass:setColor(0.7, 0.78, 0.9, 1)
    pass:text(self.detail or '', width / 2, height / 2, 0.5, 0, 0, 0, 0.5)
  end
end

function loading:keypressed(key)
  if key == 'escape' then
    if self.net then self.net:close() end
    self.nextState = 'menu'
  end
end

return loading
