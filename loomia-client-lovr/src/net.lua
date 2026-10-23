-- Canal de rede do cliente Loomia.
--
-- Usa o módulo nativo `udp` do LoomiaEngine (`require('udp')`), sem processo
-- auxiliar: um socket UDP não bloqueante, lido uma vez por frame.
--
-- O AppImage do cliente abre uma ponte local em 127.0.0.1:7788 por segurança
-- (o jogo nunca abre porta própria). O módulo aponta para lá por padrão; para
-- falar direto com a sala, passe `host`/`port` da sala em `net.new`.

local net = {}
net.__index = net

local protocol = require('src.protocol')

net.TICK_RATE = 30
net.INPUT_INTERVAL = 1 / net.TICK_RATE
net.TIMEOUT = 'timeout'

local function nowSeconds()
  return (lovr.timer and lovr.timer.getTime) and lovr.timer.getTime() or os.clock()
end

local function loadUdp()
  local ok, module = pcall(require, 'udp')
  if ok and type(module) == 'table' and module.connect then return module end
  -- Ambiente de teste/navegador pode expor o módulo em `loomia.udp`.
  local lovrNamespace = rawget(_G, 'lovr')
  if lovrNamespace and lovrNamespace.udp then return lovrNamespace.udp end
  return nil
end

function net.new(host, port)
  local self = setmetatable({}, net)
  self.host = host or '127.0.0.1'
  self.port = port or 7788
  self.udp = loadUdp()
  self.sequence = 0
  self.ack = 0
  self.pingId = 0
  self.pingMs = nil
  self.lastHeard = nil
  self.helloAt = nil
  self.closed = false
  self.onMessage = {}
  self.connected = false
  return self
end

function net:hello(token, name, appearance)
  self.token = token
  self.name = name
  self.appearance = appearance
end

--- Abre o socket. Devolve true ou false + mensagem.
function net:connect()
  if not self.udp then
    return false, 'módulo udp ausente (use o LoomiaEngine)'
  end

  local ok, err = self.udp.connect(self.host, self.port)
  if not ok then
    return false, err or 'não consegui abrir o socket UDP'
  end

  self.connected = true
  self.lastHeard = nowSeconds()
  self:sendHello()
  return true
end

--- O `hello` pode se perder no UDP: o cliente repete até receber `welcome`.
function net:sendHello()
  self.helloAt = nowSeconds()
  self:send(protocol.hello(self.token, self.name, self.appearance))
end

--- Processa uma mensagem recebida (string JSON).
function net:handle(text)
  local message = protocol.decode(text)
  if not message then return end

  self.lastHeard = nowSeconds()

  if message.t == 'welcome' then
    self.welcome = message
  elseif message.t == 'snapshot' then
    self.ack = protocol.parseSnapshot(message, self.playerId).ack
  elseif message.t == 'pong' then
    self.pingMs = (nowSeconds() - (message.time or nowSeconds())) * 1000
  end

  local handler = self.onMessage[message.t]
  if handler then handler(message) end
end

function net:send(message)
  if not self.udp or self.closed then return end
  self.udp.send(protocol.encode(message))
end

--- Lê todos os datagramas pendentes (uma vez por frame).
function net:poll()
  if not self.udp or self.closed then return end
  while true do
    local data, address = self.udp.receive()
    if not data then
      if address ~= net.TIMEOUT then
        self.lastError = address
      end
      break
    end
    self.lastAddress = address
    self:handle(data)
  end

  -- Reenvia o hello a cada 500 ms enquanto a sala não responder.
  if not self.welcome and self.helloAt and nowSeconds() - self.helloAt > 0.5 then
    self:sendHello()
  end
end

function net:ping()
  self.pingId = self.pingId + 1
  self:send(protocol.ping(self.pingId, nowSeconds()))
end

function net:chat(text)
  self:send(protocol.chat(text))
end

function net:respawn()
  self:send(protocol.respawn())
end

function net:close()
  if self.closed then return end
  self.closed = true
  if self.udp then
    pcall(function()
      self:send(protocol.bye())
      self.udp.close()
    end)
  end
end

return net
