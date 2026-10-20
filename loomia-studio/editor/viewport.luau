--[[
	Câmera orbital de viewport (estilo Studio).

	Arrastar com o botão direito (ou esquerdo quando não há ferramenta de
	seleção ativa) orbita; a roda do mouse aproxima; WASD desloca o alvo no
	plano da câmera.

	Convenção: a orientação de uma part/CFrame na Ruzit é só Euler
	(`CFrame.Angles`), então montamos a base da câmera à mão para não depender
	de `CFrame.LookAt` no pitch.
]]

local Viewport = {}
Viewport.__index = Viewport

local Primitives = import("Primitives")
local Vector = Primitives.Vector
local CFrame = Primitives.CFrame

local MIN_DISTANCE = 2
local MAX_DISTANCE = 260

function Viewport.new(camera)
	local self = setmetatable({}, Viewport)
	self.camera = camera
	self.target = { 0, 0, 0 }
	self.yaw = math.pi * 0.75
	self.pitch = -0.55
	self.distance = 46
	self:apply()
	return self
end

--- Base da câmera: frente = (-sin(yaw)cos(pitch), sin(pitch), -cos(yaw)cos(pitch)).
function Viewport:basis()
	local cp = math.cos(self.pitch)
	local sp = math.sin(self.pitch)
	local sy = math.sin(self.yaw)
	local cy = math.cos(self.yaw)
	local forward = Vector.new(-sy * cp, sp, -cy * cp)
	local right = Vector.new(cy, 0, -sy)
	local up = right:Cross(forward)
	return forward, right, up
end

function Viewport:eye()
	local forward = self:basis()
	return Vector.new(
		self.target[1] - forward.X * self.distance,
		self.target[2] - forward.Y * self.distance,
		self.target[3] - forward.Z * self.distance
	)
end

function Viewport:apply()
	local eye = self:eye()
	-- Roll = 0; yaw = atan2(-dx, -dz); pitch = asin(dy).
	local dx, dy, dz = eye.X - self.target[1], eye.Y - self.target[2], eye.Z - self.target[3]
	local yaw = math.atan2(-dx, -dz)
	local pitch = math.asin(math.max(-0.999, math.min(0.999, -dy / (self.distance > 1e-4 and self.distance or 1))))
	self.camera.CFrame = CFrame.new(eye, Vector.new(pitch, yaw, 0))
end

function Viewport:orbit(dx, dy)
	self.yaw = self.yaw - dx * 0.006
	self.pitch = math.max(-1.5, math.min(1.5, self.pitch - dy * 0.006))
	self:apply()
end

function Viewport:zoom(steps)
	self.distance = math.max(MIN_DISTANCE, math.min(MAX_DISTANCE, self.distance * (1 - steps * 0.12)))
	self:apply()
end

--- Pan no plano horizontal (relativo à câmera), em unidades por segundo.
function Viewport:pan(forwardAmount, rightAmount, dt)
	local forward, right = self:basis()
	local speed = math.max(12, self.distance * 1.6) * dt
	self.target[1] = self.target[1] + (forward.X * forwardAmount + right.X * rightAmount) * speed
	self.target[3] = self.target[3] + (forward.Z * forwardAmount + right.Z * rightAmount) * speed
	self.target[2] = math.max(-40, self.target[2] + forward.Y * forwardAmount * speed * 0.5)
	self:apply()
end

function Viewport:focus(position, size)
	self.target = { position[1], position[2], position[3] }
	local span = math.max(size[1], size[2], size[3])
	self.distance = math.max(8, math.min(MAX_DISTANCE, span * 4 + 10))
	self:apply()
end

--- Raio do centro da tela (para o picking).
function Viewport:centerRay()
	local eye = self:eye()
	local forward, right, up = self:basis()
	return eye, forward, right, up
end

return Viewport
