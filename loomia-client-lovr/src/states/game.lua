-- Estado principal: input -> previsão local -> UDP -> reconciliação -> render.
--
-- Rede:
--   • o cliente prevê o movimento com a mesma física do servidor (src/physics)
--   • guarda os inputs ainda não confirmados (seq > ack do snapshot)
--   • ao chegar um snapshot, volta para a posição autoritativa e reaplica
--     apenas os inputs pendentes (reconciliação por replay)
--
-- O snapshot NÃO traz a posição do próprio jogador (só o `ack`), então o
-- cliente guarda a última posição autoritativa recebida no `welcome`/`respawn`
-- e a usa como base da reconciliação.

local game = {}
game.__index = game

local physics = require('src.physics')
local avatar = require('src.avatar')
local camera = require('src.camera')
local hud = require('src.hud')

local TICK_DT = physics.TICK_DT

function game.new(app)
  local self = setmetatable({}, game)
  self.app = app
  self.keys = {}
  self.avatars = {}       -- id -> avatar remoto
  self.remote = {}        -- id -> { position, yaw, velocity }
  self.previous = {}      -- id -> { x, z } (velocidade estimada)
  self.speeds = {}
  self.pending = {}
  self.accumulator = 0
  self.sequence = 0
  self.dragging = false
  self.firstPerson = false
  self.chatOpen = false
  self.pingTimer = 0
  self.hud = hud.new()
  self.camera = camera.new()
  return self
end

function game:start(transfer)
  if not transfer then return end
  self.world = transfer.world
  self.net = transfer.net
  self.args = transfer.args
  self.appearance = transfer.appearance
  self.camera.yaw = (transfer.args and transfer.args.yaw) or 0
  local welcome = transfer.welcome or {}

  self.localId = welcome.id
  self.spawn = welcome.spawn or self.world.spawn
  -- O protocolo NÃO manda `you` no welcome: o servidor coloca o jogador no
  -- spawn e corrige no primeiro snapshot (`players[].p` do próprio id).
  self.state = physics.newState(self.spawn)
  self.state.yaw = self.camera.yaw
  self.authoritative = { p = self.spawn, v = { 0, 0, 0 }, ground = false }

  self.me = avatar.new({
    username = self.args.name,
    body_color = self.appearance and self.appearance.body_color,
    shirt_color = self.appearance and self.appearance.shirt_color,
    pants_color = self.appearance and self.appearance.pants_color,
    hat = self.args.hat,
  })
  self.me:update(0, self.state, 0, true)

  for _, player in ipairs(welcome.players or {}) do
    self:addRemote(player)
  end

  for _, line in ipairs(welcome.chat or {}) do
    self.hud:addChat(line.from, line.text)
  end

  self.net.onMessage.snapshot = function(message) self:onSnapshot(message) end
  self.net.onMessage.players = function(message) self:onPlayers(message) end
  self.net.onMessage.chat = function(message) self.hud:addChat(message.from, message.text) end
  self.net.onMessage.respawn = function(message) self:onRespawn(message) end
  self.net.onMessage.error = function(message) self.hud:addChat('Loomia', message.message or 'erro') end

  self:refreshPlayerList()
end

--- Cria o avatar de outro jogador (a etiqueta de nome vem do `username`).
function game:addRemote(player)
  if not player or not player.id or player.id == self.localId then return end
  local look = player.appearance
  if type(look) ~= 'table' or next(look) == nil then look = {} end
  if not look.username or look.username == '' then look.username = player.name or 'Jogador' end

  local position = player.p or self.spawn
  local av = avatar.new(look)
  self.avatars[player.id] = av
  self.remote[player.id] = {
    position = { position[1], position[2], position[3] },
    yaw = player.yaw or 0,
    velocity = { 0, 0, 0 },
  }
  av:update(0, self.remote[player.id], 0, true)
end

function game:removeRemote(id)
  local av = self.avatars[id]
  if av then
    -- Solta os modelos/nós do avatar (o LOVR coleta automaticamente).
    av.root = nil
    av.nametag = nil
  end
  self.avatars[id] = nil
  self.remote[id] = nil
  self.previous[id] = nil
  self.speeds[id] = nil
end

function game:onPlayers(message)
  local seen = {}
  for _, player in ipairs(message.players or {}) do
    seen[player.id] = true
    if player.id ~= self.localId then
      local av = self.avatars[player.id]
      if not av then
        self:addRemote({ id = player.id, name = player.name, appearance = player.appearance, p = self.spawn })
      elseif player.name and av.name ~= player.name then
        av.name = player.name
        av.nametag = lovr.graphics.newText(player.name)
      end
    end
  end
  for id in pairs(self.avatars) do
    if not seen[id] then self:removeRemote(id) end
  end
  self:refreshPlayerList()
end

function game:refreshPlayerList()
  local names = { self.me and self.me.name or (self.args and self.args.name) or 'Jogador' }
  for _, av in pairs(self.avatars) do names[#names + 1] = av.name end
  table.sort(names)
  self.hud:setPlayers(names)
end

function game:onRespawn(message)
  local position = message.p or self.spawn
  self.state = physics.newState(position)
  self.pending = {}
  self.authoritative = { p = position, v = { 0, 0, 0 }, ground = false }
end

function game:onSnapshot(message)
  -- O ack é `players[].ack` do PRÓPRIO jogador (o snapshot lista todos,
  -- inclusive quem recebeu).
  local localEntry = nil
  for _, player in ipairs(message.players or {}) do
    if player.id == self.localId then localEntry = player end
  end
  local ack = localEntry and (localEntry.ack or 0) or 0

  -- Base da reconciliação: posição autoritativa do próprio jogador.
  if localEntry and localEntry.p then
    self.authoritative = {
      p = { localEntry.p[1], localEntry.p[2], localEntry.p[3] },
      v = localEntry.v or { 0, 0, 0 },
      ground = localEntry.ground or false,
    }
  end

  -- Remove os inputs que o servidor já aplicou.
  local remaining = {}
  for _, entry in ipairs(self.pending) do
    if entry.seq > ack then remaining[#remaining + 1] = entry end
  end
  self.pending = remaining
  self.lastAck = ack

  for _, player in ipairs(message.players or {}) do
    if player.id ~= self.localId then
    local previous = self.previous[player.id]
    local estimated = 0
    if previous then
      local dx = player.p[1] - previous[1]
      local dz = player.p[3] - previous[2]
      estimated = math.sqrt(dx * dx + dz * dz) * physics.TICK_RATE
    end
    self.previous[player.id] = { player.p[1], player.p[3] }
    self.speeds[player.id] = (self.speeds[player.id] or 0) * 0.7 + estimated * 0.3

    local av = self.avatars[player.id]
    if not av then
      self:addRemote({
        id = player.id,
        name = player.name,
        appearance = player.appearance,
        p = player.p,
        yaw = player.yaw,
      })
      av = self.avatars[player.id]
    end
    if av then
      self.remote[player.id] = {
        position = { player.p[1], player.p[2], player.p[3] },
        yaw = player.yaw or 0,
        velocity = player.v or { 0, 0, 0 },
      }
    end
    end
  end
end

--- Reconciliação por replay: parte do estado autoritativo e reaplica os
-- inputs ainda não confirmados. A diferença é amortizada, então não há
-- "puxão" quando o servidor e o cliente divergem por alguns centímetros.
function game:applyReconciliation()
  local origin = self.authoritative
  if not origin then return end

  local predicted = physics.newState(origin.p)
  predicted.velocity = { origin.v[1], origin.v[2], origin.v[3] }
  predicted.onGround = origin.ground
  predicted.yaw = self.state.yaw
  for _, entry in ipairs(self.pending) do
    predicted = physics.step(predicted, entry, self.world.blocks, self.world.minY, TICK_DT)
  end

  local dx = predicted.position[1] - self.state.position[1]
  local dy = predicted.position[2] - self.state.position[2]
  local dz = predicted.position[3] - self.state.position[3]
  if math.sqrt(dx * dx + dz * dz) > 6 or math.abs(dy) > 6 then
    -- Teleporte (respawn do servidor).
    self.state.position = predicted.position
    self.state.velocity = predicted.velocity
  else
    local k = 0.25
    self.state.position[1] = self.state.position[1] + dx * k
    self.state.position[2] = self.state.position[2] + dy * k
    self.state.position[3] = self.state.position[3] + dz * k
    self.state.velocity = predicted.velocity
  end
  self.state.onGround = predicted.onGround or self.state.onGround
end

-- ---------------------------------------------------------------------------
-- Input
-- ---------------------------------------------------------------------------

local MOVE_KEYS = {
  w = true, arrowup = true,
  s = true, arrowdown = true,
  a = true, arrowleft = true,
  d = true, arrowright = true,
}

function game:keypressed(key)
  if self.chatOpen then
    local result = self.hud:keypressed(key)
    if result == 'cancel' then
      self.chatOpen = false
      self.hud:setChatOpen(false)
    elseif result and result ~= '' then
      self.net:chat(result)
      self.chatOpen = false
      self.hud:setChatOpen(false)
    end
    return
  end

  if MOVE_KEYS[key] or key == 'space' then
    self.keys[key] = true
  elseif key == 't' or key == 'return' then
    self.chatOpen = true
    self.hud:setChatOpen(true)
  elseif key == 'v' then
    self.firstPerson = not self.firstPerson
  elseif key == 'r' then
    self.net:respawn()
  elseif key == 'f1' then
    self.hud.showDebug = not self.hud.showDebug
  elseif key == 'escape' then
    -- Volta para o launcher (e não fecha o cliente inteiro).
    self:leave()
  end
end
--- Encerra a sessão e volta para o launcher do cliente.
function game:leave()
  if self.net then
    pcall(function() self.net:close() end)
    self.net = nil
  end
  -- Solta os avatares remotos para a próxima sala começar limpa.
  self.avatars = {}
  self.remote = {}
  self.previous = {}
  self.speeds = {}
  self.pending = {}
  self.app.state = 'launcher'
  if self.app.launcher then self.app.launcher:enter() end
end

function game:keyreleased(key)
  if MOVE_KEYS[key] or key == 'space' then
    self.keys[key] = false
  end
end

function game:textinput(text)
  if self.chatOpen then self.hud:textinput(text) end
end

function game:mousemoved(x, y, dx, dy)
  if self.dragging and not self.chatOpen then
    self.camera:rotate(dx, dy)
  end
end

function game:mousepressed(x, y, button)
  if button == 2 then self.dragging = true end
end

function game:mousereleased(x, y, button)
  if button == 2 then self.dragging = false end
end

function game:wheelmoved(dx, dy)
  self.camera:zoom(dy)
end

function game:readMoveAxes()
  local forward = (self.keys.w or self.keys.arrowup) and 1 or 0
  local back = (self.keys.s or self.keys.arrowdown) and 1 or 0
  local left = (self.keys.a or self.keys.arrowleft) and 1 or 0
  local right = (self.keys.d or self.keys.arrowright) and 1 or 0
  local z = forward - back
  local x = right - left
  if x == 0 and z == 0 then return 0, 0 end
  -- Transforma o input local (frente/trás/lados) no yaw da câmera.
  local fx, fz = self.camera:forward()
  local rx, rz = fz, -fx
  local moveX = fx * z + rx * x
  local moveZ = fz * z + rz * x
  local length = math.sqrt(moveX * moveX + moveZ * moveZ)
  if length > 0 then moveX, moveZ = moveX / length, moveZ / length end
  return moveX, moveZ
end

-- ---------------------------------------------------------------------------
-- Loop
-- ---------------------------------------------------------------------------

function game:update(dt)
  if not self.net then return end

  self.net:poll()
  self:applyReconciliation()

  self.pingTimer = self.pingTimer + dt
  if self.pingTimer > 1 then
    self.pingTimer = 0
    self.net:ping()
  end

  if self.firstPerson then
    self.state.yaw = self.camera.yaw
  end

  -- Previsão local a 30 Hz (um input por tique, como o servidor).
  self.accumulator = self.accumulator + dt
  local jump = self.keys.space and not self.chatOpen
  while self.accumulator >= TICK_DT do
    self.accumulator = self.accumulator - TICK_DT
    local moveX, moveZ = 0, 0
    if not self.chatOpen then moveX, moveZ = self:readMoveAxes() end
    self.state = physics.step(self.state, { moveX = moveX, moveZ = moveZ, jump = jump }, self.world.blocks, self.world.minY, TICK_DT)
    if self.firstPerson then self.state.yaw = self.camera.yaw end

    self.sequence = self.sequence + 1
    self.net:send({
      t = 'input',
      seq = self.sequence,
      move_x = moveX,
      move_z = moveZ,
      jump = jump and true or false,
      yaw = self.state.yaw,
    })
    self.pending[#self.pending + 1] = { seq = self.sequence, moveX = moveX, moveZ = moveZ, jump = jump }
    if #self.pending > 120 then table.remove(self.pending, 1) end

    if physics.fellOut(self.state, self.world.minY) then self.net:respawn() end
  end

  self.me:update(dt, self.state, math.sqrt(self.state.velocity[1] ^ 2 + self.state.velocity[3] ^ 2), true)

  for id, remote in pairs(self.remote) do
    local av = self.avatars[id]
    if av then av:update(dt, remote, self.speeds[id] or 0) end
  end

  self.camera:update(dt, self.state.position)
  self.hud:update(dt)
end

function game:draw(pass)
  self.camera:apply()
  self.world:draw(pass)
  self.me:draw(pass)
  for _, av in pairs(self.avatars) do av:draw(pass) end

  local fps = 1 / math.max(0.001, lovr.timer.getDelta())
  local debug = {
    ('FPS: %.0f'):format(fps),
    ('ping: %s'):format(self.net.pingMs and ('%.0f ms'):format(self.net.pingMs) or '—'),
    ('seq: %d  ack: %d'):format(self.sequence, self.net.ack or 0),
    ('posição: %.1f %.1f %.1f'):format(self.state.position[1], self.state.position[2], self.state.position[3]),
    ('jogadores: %d'):format(#self.hud.playerNames),
    ('blocos: %d'):format(self.world.blockCount),
  }
  self.hud:draw(pass, debug)
end

function game:quit()
  if self.net then self.net:close() end
end

return game
