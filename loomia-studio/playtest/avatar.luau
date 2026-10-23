--[[
	Avatar de teste do Studio (o "boneco" do Playtest).

	Mesmo modelo 2008 do cliente, mas sem skin: o playtest usa um avatar
	padrão e expõe a posição/estado via `import("Players").LocalPlayer` para
	que os scripts do mapa possam reagir (checkpoints, teleportes, HUD...).
]]

local Avatar = {}
Avatar.__index = Avatar

local Renderable = import("Renderable")
local Primitives = import("Primitives")

local Vector = Primitives.Vector
local CFrame = Primitives.CFrame
local Color3 = Primitives.Color3

local HEAD_TOP = 3.59

local function rgb(color)
	return Color3.new(color[1] / 255, color[2] / 255, color[3] / 255)
end

local function cube(size, color)
	local part = Renderable.BasePart("Cube")
	part.Size = Vector.new(size[1], size[2], size[3])
	part.Color = color
	part.CastShadow = true
	part.ReceiveShadow = true
	part.Lit = true
	part.IgnoreInRaycast = true
	return part
end

--- Cria o boneco. `look` = { bodyColor, shirtColor, pantsColor, hat }.
function Avatar.new(look)
	local self = setmetatable({}, Avatar)
	self.look = look or {}
	self.parts = {}
	self.legs = {}
	self.arms = {}
	self.position = { 0, 0, 0 }
	self.yaw = 0
	self.clock = 0
	self.speed = 0
	self:build()
	return self
end

function Avatar:add(part, offset)
	self.parts[#self.parts + 1] = { part = part, x = offset[1], y = offset[2], z = offset[3] }
	return part
end

function Avatar:build()
	local skin = rgb(self.look.bodyColor or { 235, 190, 25 })
	local shirt = rgb(self.look.shirtColor or { 15, 120, 190 })
	local pants = rgb(self.look.pantsColor or { 35, 75, 45 })

	for _, side in ipairs({ { phase = 1.0, x = -0.25 }, { phase = -1.0, x = 0.25 } }) do
		self.legs[#self.legs + 1] = {
			part = self:add(cube({ 0.5, 1.0, 0.5 }, pants), { side.x, 1.0, 0 }),
			phase = side.phase,
			x = side.x,
		}
	end

	self:add(cube({ 1.0, 1.4, 0.6 }, shirt), { 0, 1.7, 0 })

	for _, side in ipairs({ { phase = 1.0, x = -0.72 }, { phase = -1.0, x = 0.72 } }) do
		self.arms[#self.arms + 1] = {
			sleeve = self:add(cube({ 0.45, 1.1, 0.45 }, shirt), { side.x, 2.3, 0 }),
			hand = self:add(cube({ 0.42, 0.24, 0.42 }, skin), { side.x, 2.3, 0 }),
			phase = side.phase,
			x = side.x,
		}
	end

	self:add(cube({ 1.22, 1.22, 1.22 }, skin), { 0, 2.98, 0 })
	for _, x in ipairs({ -0.25, 0.25 }) do
		self:add(cube({ 0.17, 0.2, 0.04 }, Color3.new(20 / 255, 20 / 255, 25 / 255)), { x, 3.08, 0.62 })
	end
	self:add(cube({ 0.4, 0.06, 0.04 }, Color3.new(70 / 255, 35 / 255, 35 / 255)), { 0, 2.72, 0.62 })

	if self.look.hat == "cap" or self.look.hat == nil then
		self:add(cube({ 1.28, 0.36, 1.28 }, Color3.new(200 / 255, 45 / 255, 45 / 255)), { 0, HEAD_TOP + 0.14, 0 })
	end

	self:apply(0, 0)
end

--- Escreve posição/yaw/speed nas parts (chamado a cada frame).
function Avatar:apply(dt, speed)
	local root = CFrame.new(Vector.new(self.position[1], self.position[2], self.position[3]), Vector.new(0, self.yaw, 0))
	self.speed = speed or 0
	if self.speed > 0.5 then
		self.clock = self.clock + dt * (4.0 + self.speed * 0.55)
	end
	local swing = self.speed > 0.5 and math.sin(self.clock) * 0.65 or 0

	for _, entry in ipairs(self.parts) do
		entry.part.CFrame = root * CFrame.new(Vector.new(entry.x, entry.y, entry.z), Vector.new(0, 0, 0))
	end
	for _, leg in ipairs(self.legs) do
		leg.part.CFrame = root
			* CFrame.new(Vector.new(leg.x, 1.0, 0), Vector.new(0, 0, 0))
			* CFrame.Angles(swing * leg.phase, 0, 0)
			* CFrame.new(Vector.new(0, -0.5, 0), Vector.new(0, 0, 0))
	end
	for _, arm in ipairs(self.arms) do
		local pivot = root
			* CFrame.new(Vector.new(arm.x, 2.3, 0), Vector.new(0, 0, 0))
			* CFrame.Angles(-swing * arm.phase, 0, 0)
		arm.sleeve.CFrame = pivot * CFrame.new(Vector.new(0, -0.55, 0), Vector.new(0, 0, 0))
		arm.hand.CFrame = pivot * CFrame.new(Vector.new(0, -1.18, 0), Vector.new(0, 0, 0))
	end
end

function Avatar:setPosition(x, y, z)
	self.position = { x, y, z }
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
