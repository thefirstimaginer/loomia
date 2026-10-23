-- Avatar do jogador: caixas no estilo Roblox 2008 + etiqueta de nome.
--
-- A etiqueta usa `lovr.graphics.newText` (MakeText), o equivalente do LOVR ao
-- Billboard do Roblox: o texto é desenhado sempre virado para a câmera.
--
-- Altura (pés em y = 0):
--   pernas 0.0 – 1.0   torso 1.0 – 2.4   cabeça 2.4 – 3.6   nome em 4.1

local avatar = {}
avatar.__index = avatar

avatar.HEAD_TOP = 3.6
avatar.NAME_HEIGHT = 4.15
avatar.COLORS = {
  body = { 234, 190, 25 },
  shirt = { 15, 120, 190 },
  pants = { 35, 75, 45 },
  hat = { 200, 40, 40 },
}

local function colorOf(list, fallback)
  list = list or fallback
  return lovr.math.newVec3(list[1] / 255, list[2] / 255, list[3] / 255)
end

local function makeBox(size, color, x, y, z, parent)
  local model = lovr.graphics.newModel()
  model:addMesh(lovr.graphics.newBox(size[1], size[2], size[3]))
    :setMaterial(lovr.graphics.newMaterial(color))
  model:setPosition(x, y, z)
  model:setParent(parent)
  return model
end

--- Cria o avatar e vincula tudo a um nó `root` (posição + rotação do corpo).
function avatar.new(appearance, opts)
  local self = setmetatable({}, avatar)
  self.appearance = appearance or {}
  local look = self.appearance

  local body = colorOf(look.body_color, avatar.COLORS.body)
  local shirt = colorOf(look.shirt_color, avatar.COLORS.shirt)
  local pants = colorOf(look.pants_color, avatar.COLORS.pants)

  self.root = lovr.graphics.newModel()
  self.root:setPosition(0, 0, 0)

  self.legs = {}
  for index, x in ipairs({ -0.25, 0.25 }) do
    local pivot = lovr.graphics.newModel()
    pivot:setPosition(x, 1.0, 0)
    pivot:setParent(self.root)
    local leg = makeBox({ 0.5, 1.0, 0.5 }, pants, 0, -0.5, 0, pivot)
    self.legs[index] = { pivot = pivot, model = leg }
  end

  self.torso = makeBox({ 1.0, 1.4, 0.6 }, shirt, 0, 1.7, 0, self.root)
  self.head = makeBox({ 1.22, 1.22, 1.22 }, body, 0, 3.0, 0, self.root)

  self.arms = {}
  for index, x in ipairs({ -0.75, 0.75 }) do
    local pivot = lovr.graphics.newModel()
    pivot:setPosition(x, 2.3, 0)
    pivot:setParent(self.root)
    local sleeve = makeBox({ 0.4, 1.0, 0.6 }, shirt, 0, -0.5, 0, pivot)
    makeBox({ 0.4, 0.4, 0.4 }, body, 0, -1.05, 0, pivot)
    self.arms[index] = { pivot = pivot, model = sleeve }
  end

  -- Chapéu opcional.
  local hat = look.hat
  if hat == 'cap' then
    makeBox({ 1.3, 0.3, 1.3 }, colorOf(nil, avatar.COLORS.hat), 0, 3.75, 0, self.root)
    makeBox({ 1.2, 0.1, 0.5 }, colorOf(nil, avatar.COLORS.hat), 0, 3.62, 0.8, self.root)
  elseif hat == 'top_hat' then
    makeBox({ 1.6, 0.15, 1.6 }, lovr.math.newVec3(0.08, 0.08, 0.1), 0, 3.68, 0, self.root)
    makeBox({ 1.0, 1.0, 1.0 }, lovr.math.newVec3(0.08, 0.08, 0.1), 0, 4.25, 0, self.root)
  elseif hat == 'crown' then
    makeBox({ 1.3, 0.3, 1.3 }, lovr.math.newVec3(0.95, 0.78, 0.15), 0, 3.75, 0, self.root)
  elseif hat == 'propeller' then
    makeBox({ 1.3, 0.3, 1.3 }, lovr.math.newVec3(0.98, 0.78, 0.15), 0, 3.75, 0, self.root)
    self.propeller = makeBox({ 1.4, 0.08, 0.2 }, avatar.COLORS.body[1] and lovr.math.newVec3(0.2, 0.6, 0.9), 0, 4.05, 0, self.root)
  end

  -- Etiqueta de nome (frente/costas para ler dos dois lados).
  self.name = look.username or look.name or 'Jogador'
  if self.name == '' then self.name = 'Jogador' end
  self.nameSize = (opts and opts.nameSize) or 0.9
  self.nameColor = (opts and opts.nameColor) or { r = 1, g = 1, b = 1 }
  self.nametag = lovr.graphics.newText(self.name)
  self.clock = math.random() * math.pi * 2
  self.position = { 0, 0, 0 }
  self.yaw = 0
  self.speed = 0
  self.targetPosition = { 0, 0, 0 }
  self.targetYaw = 0
  return self
end

--- Atualiza posição (interpolada), yaw e a caminhada.
-- `target` = { position = {x,y,z}, yaw = n }; `snap` cola na posição.
function avatar:update(dt, target, speed, snap)
  dt = math.max(0, dt)
  local tx, ty, tz = target.position[1], target.position[2], target.position[3]
  local dx, dz = tx - self.position[1], tz - self.position[3]
  local far = math.sqrt(dx * dx + dz * dz) > 10 or math.abs(ty - self.position[2]) > 6
  local k = (snap or far) and 1 or math.min(1, dt * 20)
  self.position[1] = self.position[1] + dx * k
  self.position[2] = self.position[2] + (ty - self.position[2]) * k
  self.position[3] = self.position[3] + dz * k

  local targetYaw = target.yaw or 0
  local delta = targetYaw - self.yaw
  while delta > math.pi do delta = delta - math.pi * 2 end
  while delta < -math.pi do delta = delta + math.pi * 2 end
  self.yaw = self.yaw + delta * math.min(1, dt * 8)

  self.speed = speed or self.speed or 0
  if self.speed > 0.5 then
    self.clock = self.clock + dt * (3.0 + self.speed * 0.35)
  end
  local swing = self.speed > 0.5 and math.sin(self.clock) * 0.42 or 0

  self.root:setPosition(self.position[1], self.position[2], self.position[3])
  self.root:setOrientation(self.yaw, 0, 0)

  self.legs[1].pivot:setOrientation(swing, 0, 0)
  self.legs[2].pivot:setOrientation(-swing, 0, 0)
  self.arms[1].pivot:setOrientation(-swing, 0, 0)
  self.arms[2].pivot:setOrientation(swing, 0, 0)

  if self.propeller then
    local spin = lovr.timer.getTime() * 6
    self.propeller:setOrientation(spin, 0, 0)
  end
end

--- Desenha o avatar e a etiqueta de nome.
function avatar:draw(pass)
  pass:draw(self.root, 1, 1, 1, 1)
  if self.nametag then
    local x, y, z = self.position[1], self.position[2] + avatar.NAME_HEIGHT, self.position[3]
    -- Frente (olhando para a câmera padrão).
    pass:text(self.nametag, self.nameSize, x, y, z, 0, 0, 0, self.nameColor.r, self.nameColor.g, self.nameColor.b, 1)
    -- Costas.
    pass:text(self.nametag, self.nameSize, x, y, z, math.pi, 0, 0, self.nameColor.r, self.nameColor.g, self.nameColor.b, 1)
  end
end

return avatar
