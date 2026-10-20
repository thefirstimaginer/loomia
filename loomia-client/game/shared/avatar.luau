--[[
	Avatares dos jogadores no estilo 2008 (port de `loomia-client/src/avatar.rs`).

	```text
	         [ chapéu ]        <- opcional (boné, cartola, hélice, coroa)
	         [ cabeça ] 1.22²
	[ braço ][ torso ][ braço ]   torso 1.0 x 1.4 x 0.6
	         [ pernas DS ]        pernas 0.5 x 1.0
	```

	A origem do avatar fica entre os pés (igual a `CharacterState.position`), então
	o CFrame é escrito direto da física. Braços e pernas balançam conforme a
	velocidade horizontal; o avatar inteiro gira com o `yaw`.

	O avatar encara +Z no espaço local (mesma convenção de parts da Ruzit); o yaw
	da física é `atan2(x, z)`.

	Estratégia de animação: cada part guarda o offset LOCAL e, a cada frame,
	aplicamos `raiz * offsetLocal`. Sem hierarquia de transforms (a Ruzit não
	tem) e sem alocar tabelas no loop.
]]

local Avatar = {}
Avatar.__index = Avatar

local Renderable = import("Renderable")
local Primitives = import("Primitives")

local Vector = Primitives.Vector
local CFrame = Primitives.CFrame
local Color3 = Primitives.Color3

local HEAD_TOP = 3.59
local HALF_PI = math.pi * 0.5

local function rgb(color, fallback)
	color = color or fallback
	return Color3.new(color[1] / 255, color[2] / 255, color[3] / 255)
end

local function cube(size, color)
	local part = Renderable.BasePart("Cube")
	part.Size = Vector.new(size[1], size[2], size[3])
	part.Color = color
	part.CastShadow = true
	part.ReceiveShadow = true
	part.Lit = true
	return part
end

--- Cria um avatar completo em torno da origem (pés em y = 0).
function Avatar.new(appearance)
	local self = setmetatable({}, Avatar)
	self.appearance = appearance or {}
	self.parts = {}
	self.legs = {}
	self.arms = {}
	self.position = { 0, 0, 0 }
	self.yaw = nil
	self.root = CFrame.new(Vector.new(0, 0, 0), Vector.new(0, 0, 0))
	self.clock = math.random() * math.pi * 2
	self.speed = 0
	self.spin = 0
	self:build()
	return self
end

--- Registra uma part com offset local `{ x, y, z }`.
function Avatar:add(part, offset)
	self.parts[#self.parts + 1] = { part = part, x = offset[1], y = offset[2], z = offset[3] }
	return part
end

function Avatar:build()
	local look = self.appearance
	local skin = rgb(look.bodyColor or look.body_color, { 235, 190, 25 })
	local shirt = rgb(look.shirtColor or look.shirt_color, { 15, 120, 190 })
	local pants = rgb(look.pantsColor or look.pants_color, { 35, 75, 45 })

	-- Pernas (y de 0 a 1). O pivô fica no quadril (y = 1).
	for _, side in ipairs({ { phase = 1.0, x = -0.25 }, { phase = -1.0, x = 0.25 } }) do
		self.legs[#self.legs + 1] = {
			part = self:add(cube({ 0.5, 1.0, 0.5 }, pants), { side.x, 1.0, 0 }),
			phase = side.phase,
			x = side.x,
		}
	end

	-- Torso (y de 1.0 a 2.4).
	self:add(cube({ 1.0, 1.4, 0.6 }, shirt), { 0, 1.7, 0 })

	-- Braços com pivô no ombro (y = 2.3).
	for _, side in ipairs({ { phase = 1.0, x = -0.72 }, { phase = -1.0, x = 0.72 } }) do
		self.arms[#self.arms + 1] = {
			sleeve = self:add(cube({ 0.45, 1.1, 0.45 }, shirt), { side.x, 2.3, 0 }),
			hand = self:add(cube({ 0.42, 0.24, 0.42 }, skin), { side.x, 2.3, 0 }),
			phase = side.phase,
			x = side.x,
		}
	end

	-- Cabeça (y de 2.4 a 3.6).
	self:add(cube({ 1.22, 1.22, 1.22 }, skin), { 0, 2.98, 0 })

	-- Olhos e sorriso na face +Z.
	for _, x in ipairs({ -0.25, 0.25 }) do
		self:add(cube({ 0.17, 0.2, 0.04 }, Color3.new(20 / 255, 20 / 255, 25 / 255)), { x, 3.08, 0.62 })
	end
	self:add(cube({ 0.4, 0.06, 0.04 }, Color3.new(70 / 255, 35 / 255, 35 / 255)), { 0, 2.72, 0.62 })

	self:buildHat(look.hat)
	self:update(0, { position = self.position, yaw = 0 }, 0)
end

function Avatar:buildHat(hat)
	if not hat then
		return
	end
	hat = tostring(hat)
	if hat == "cap" then
		self:add(cube({ 1.28, 0.36, 1.28 }, Color3.new(200 / 255, 45 / 255, 45 / 255)), { 0, HEAD_TOP + 0.14, 0 })
		self:add(cube({ 1.05, 0.09, 0.65 }, Color3.new(160 / 255, 35 / 255, 35 / 255)), { 0, HEAD_TOP - 0.02, 0.82 })
	elseif hat == "top_hat" or hat == "topHat" then
		self:add(cube({ 1.6, 0.12, 1.6 }, Color3.new(25 / 255, 25 / 255, 30 / 255)), { 0, HEAD_TOP + 0.03, 0 })
		self:add(cube({ 1.0, 1.2, 1.0 }, Color3.new(30 / 255, 30 / 255, 36 / 255)), { 0, HEAD_TOP + 0.7, 0 })
	elseif hat == "propeller" then
		self:add(cube({ 1.26, 0.3, 1.26 }, Color3.new(250 / 255, 200 / 255, 40 / 255)), { 0, HEAD_TOP + 0.12, 0 })
		self.propeller = { y = HEAD_TOP + 0.36 }
		self.propellerParts = {}
		-- Duas pás cruzadas; giram juntas porque compartilham o mesmo pivô.
		self:add(cube({ 1.5, 0.06, 0.22 }, Color3.new(220 / 255, 60 / 255, 60 / 255)), { 0, HEAD_TOP + 0.36, 0 })
		self:add(cube({ 0.22, 0.06, 1.5 }, Color3.new(220 / 255, 60 / 255, 60 / 255)), { 0, HEAD_TOP + 0.36, 0 })
		self.propellerParts[#self.propellerParts + 1] = self.parts[#self.parts - 1].part
		self.propellerParts[#self.propellerParts + 1] = self.parts[#self.parts].part
	elseif hat == "crown" then
		self:add(cube({ 1.2, 0.26, 1.2 }, Color3.new(240 / 255, 195 / 255, 60 / 255)), { 0, HEAD_TOP + 0.12, 0 })
		for _, tip in ipairs({ { -0.42, -0.42 }, { 0.42, -0.42 }, { -0.42, 0.42 }, { 0.42, 0.42 } }) do
			self:add(
				cube({ 0.2, 0.3, 0.2 }, Color3.new(240 / 255, 195 / 255, 60 / 255)),
				{ tip[1], HEAD_TOP + 0.38, tip[2] }
			)
		end
	end
end

--- Atualiza posição (interpolada), yaw e animação de andar.
--- `target` = { position = { x, y, z }, yaw = número } (estado autoritativo/previsão).
function Avatar:update(dt, target, speed)
	dt = math.max(0, dt)

	local tx, ty, tz = target.position[1], target.position[2], target.position[3]
	local dx, dz = tx - self.position[1], tz - self.position[3]
	local far = math.sqrt(dx * dx + dz * dz) > 10 or math.abs(ty - self.position[2]) > 6
	local k = far and 1 or math.min(1, dt * 20)
	self.position[1] = self.position[1] + dx * k
	self.position[2] = self.position[2] + (ty - self.position[2]) * k
	self.position[3] = self.position[3] + dz * k

	local targetYaw = target.yaw or 0
	local delta = targetYaw - (self.yaw or targetYaw)
	while delta > math.pi do
		delta -= math.pi * 2
	end
	while delta < -math.pi do
		delta += math.pi * 2
	end
	self.yaw = (self.yaw or targetYaw) + delta * math.min(1, dt * 14)

	self.speed = speed or self.speed or 0
	if self.speed > 0.5 then
		self.clock = self.clock + dt * (4.0 + self.speed * 0.55)
	end
	local swing = self.speed > 0.5 and math.sin(self.clock) * 0.65 or 0

	self.root = CFrame.new(Vector.new(self.position[1], self.position[2], self.position[3]), Vector.new(0, self.yaw, 0))

	-- Parts estáticas (torso, cabeça, olhos, chapéu...).
	for _, entry in ipairs(self.parts) do
		entry.part.CFrame = self.root * CFrame.new(Vector.new(entry.x, entry.y, entry.z), Vector.new(0, 0, 0))
	end

	-- Pernas: giram em torno do quadril (y = 1).
	for _, leg in ipairs(self.legs) do
		local pivot = self.root * CFrame.new(Vector.new(leg.x, 1.0, 0), Vector.new(0, 0, 0))
		leg.part.CFrame = pivot
			* CFrame.Angles(swing * leg.phase, 0, 0)
			* CFrame.new(Vector.new(0, -0.5, 0), Vector.new(0, 0, 0))
	end

	-- Braços: giram em torno do ombro (y = 2.3), em oposição às pernas.
	for _, arm in ipairs(self.arms) do
		local pivot = self.root
			* CFrame.new(Vector.new(arm.x, 2.3, 0), Vector.new(0, 0, 0))
			* CFrame.Angles(-swing * arm.phase, 0, 0)
		arm.sleeve.CFrame = pivot * CFrame.new(Vector.new(0, -0.55, 0), Vector.new(0, 0, 0))
		arm.hand.CFrame = pivot * CFrame.new(Vector.new(0, -1.18, 0), Vector.new(0, 0, 0))
	end

	-- Hélice do chapéu: gira no eixo Y em torno do próprio centro.
	if self.propeller then
		self.spin = self.spin + dt * 6.0
		local hub = self.root * CFrame.new(Vector.new(0, self.propeller.y, 0), Vector.new(0, self.spin, 0))
		for _, blade in ipairs(self.propellerParts) do
			blade.CFrame = hub
		end
	end
end

function Avatar:Destroy()
	for _, entry in ipairs(self.parts) do
		pcall(function()
			entry.part:Destroy()
		end)
	end
	self.parts = {}
	self.legs = {}
	self.arms = {}
end

return Avatar
