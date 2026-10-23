-- Canal de rede do cliente (Lua para embedding no LOVR).
--
-- O LOVR não expõe UDP na API padrão. Este módulo abre um processo auxiliar
-- (socket UDP real) e troca mensagens JSON por pipe stdin/stdout:
--
--     lua src/netbridge.lua <host> <port>
--
--   cliente → bridge   {"type":"connect", ...}
--                      {"type":"send", "data":"<json>"}
--                      {"type":"close"}
--                      {"type":"quit"}
--
--   bridge → cliente   {"type":"up"}
--                      {"type":"down", "reason":"..."}
--                      {"type":"data", "data":"<json>"}
--
-- O fio do servidor (UDP) é feito pelo `src/netbridge.lua` com LuaSocket.
--
-- IMPORTANTE: quando houver ponte nativa (módulo C que implementa
-- `connect/send/close/update`), ela é usada; caso contrário, o módulo assume
-- que o ambiente fornece `udp` (ver README) e falha com mensagem clara.

local net = {}
net.__index = net

net.TICK_RATE = 30
net.INPUT_INTERVAL = 1 / net.TICK_RATE

local function nowSeconds()
  return (lovr.timer and lovr.timer.getTime) and lovr.timer.getTime() or os.clock()
end

--- `transport` é a ponte (padrão: a do LOVR/ambiente).
-- Deve implementar:
--   connect(config, callbacks) -> true | false, erro
--   send(json) -> void
--   close() -> void
--   update() -> void (chamada a cada frame; dispara callbacks de dados)
local function defaultTransport()
  -- `lovr.udp` não existe; quem integrar a ponte nativa registra o módulo.
  local ok, module = pcall(require, 'loomiaudp')
  if ok and type(module) == 'table' then return module end
  return nil
end

function net.new(host, port, transport)
  local self = setmetatable({}, net)
  self.host = host
  self.port = port
  self.transport = transport or defaultTransport()
  self.sequence = 0
  self.pingId = 0
  self.pingMs = nil
  self.lastHeard = nil
  self.ack = 0
  self.tick = 0
  self.closed = false
  self.onMessage = {}
  self.welcomeAt = nil
  return self
end

function net:hello(token, name, appearance)
  self.token = token
  self.name = name
  self.appearance = appearance
end

--- Conecta e envia o `hello`. Devolve true ou false + mensagem.
function net:connect()
  if not self.transport then
    return false, 'rede UDP indisponível neste cliente (veja loomia-client-lovr/README.md)'
  end

  local ok, err = self.transport.connect({ host = self.host, port = self.port }, {
    onUp = function() self.connected = true end,
    onDown = function(reason)
      self.connected = false
      self.lastError = reason
    end,
    onData = function(text) self:handleLine(text) end,
  })
  if not ok then
    return false, err or 'não foi possível abrir a conexão UDP'
  end

  self.connected = true
  self.lastHeard = nowSeconds()
  self:send({
    t = 'hello',
    token = self.token,
    name = self.name,
    appearance = self.appearance,
  })
  return true
end

--- Processa uma mensagem JSON recebida (chamada pela ponte).
function net:handleLine(text)
  local message
  local ok, decoded = pcall(lovr.data.decode, 'json', text)
  if ok and type(decoded) == 'table' then
    message = decoded
  else
    ok, message = pcall(require('src.json').decode, text)
  end
  if not ok or type(message) ~= 'table' or not message.t then
    return
  end

  self.lastHeard = nowSeconds()
  if message.t == 'snapshot' then
    self.tick = message.tick or self.tick
    self.ack = message.ack or self.ack
  elseif message.t == 'welcome' then
    self.welcomeAt = nowSeconds()
  elseif message.t == 'pong' then
    self.pingMs = (nowSeconds() - (message.time or nowSeconds())) * 1000
  end

  local handler = self.onMessage[message.t]
  if handler then handler(message) end
  if self.onMessage['*'] then self.onMessage['*'](message) end
end

--- Serializa e envia uma mensagem para a sala.
function net:send(message)
  if not self.transport or self.closed then return end
  local ok, encoded = pcall(lovr.data.encode, message, 'json')
  if not ok or not encoded then
    encoded = require('src.json').encode(message)
  end
  self.transport.send(encoded)
end

--- Deve ser chamado a cada frame (a ponte pode ter dados pendentes).
function net:poll()
  if self.transport and self.transport.update then self.transport.update() end
end

function net:ping()
  self.pingId = self.pingId + 1
  self:send({ t = 'ping', id = self.pingId, time = nowSeconds() })
end

function net:chat(text)
  self:send({ t = 'chat', text = text })
end

function net:respawn()
  self:send({ t = 'respawn' })
end

function net:close()
  if self.closed then return end
  self.closed = true
  if self.transport then
    pcall(function()
      self:send({ t = 'bye' })
      self.transport.close()
    end)
  end
end

return net
