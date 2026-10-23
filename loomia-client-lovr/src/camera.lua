-- Câmera orbital em 3ª pessoa / primeira pessoa.
--
-- A câmera tem yaw PRÓPRIO (só o mouse muda). O WASD é aplicado nesse yaw,
-- então o personagem anda para onde a câmera aponta, e o corpo gira para a
-- direção do movimento sem arrastar a câmera junto.

local camera = {}
camera.__index = camera

camera.EYE_HEIGHT = 1.55

function camera.new()
  local self = setmetatable({}, camera)
  self.yaw = 0
  self.pitch = -0.35
  self.distance = 11
  self.distanceTarget = 11
  self.firstPerson = false
  self.target = { 0, 0, 0 }
  self.sensitivity = 0.005
  return self
end

function camera:rotate(dx, dy)
  self.yaw = self.yaw - dx * self.sensitivity
  local pitch = self.pitch - dy * self.sensitivity
  self.pitch = math.max(-1.2, math.min(1.2, pitch))
end

function camera:zoom(delta)
  self.distanceTarget = math.max(3, math.min(24, self.distanceTarget + delta))
end

function camera:update(dt, position)
  self.target[1] = position[1]
  self.target[2] = position[2] + camera.EYE_HEIGHT
  self.target[3] = position[3]
  self.distance = self.distance + (self.distanceTarget - self.distance) * math.min(1, dt * 12)
end

--- Direção "frente" no plano XZ (usada para transformar WASD em movimento).
function camera:forward()
  return math.sin(self.yaw), math.cos(self.yaw)
end

--- Aplica a câmera ao frame atual (o LOVR já desenha numa câmera própria).
function camera:apply()
  if self.firstPerson then
    local pitch = math.max(-1.2, math.min(1.2, self.pitch))
    local dirX = math.sin(self.yaw) * math.cos(pitch)
    local dirY = -math.sin(pitch)
    local dirZ = math.cos(self.yaw) * math.cos(pitch)
    lovr.graphics.setCamera({
      position = self.target,
      look = { self.target[1] + dirX, self.target[2] + dirY, self.target[3] + dirZ },
      up = { 0, 1, 0 },
    })
    return
  end

  local pitch = math.max(-0.45, math.min(-0.05, self.pitch))
  local distance = self.distance
  local horizontal = math.cos(pitch) * distance
  local eye = {
    self.target[1] - math.sin(self.yaw) * horizontal,
    self.target[2] - math.sin(pitch) * distance,
    self.target[3] - math.cos(self.yaw) * horizontal,
  }
  lovr.graphics.setCamera({
    position = eye,
    look = self.target,
    up = { 0, 1, 0 },
  })
  self.eye = eye
end

return camera
