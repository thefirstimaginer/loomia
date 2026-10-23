--[[
	Cena do Studio: renderiza os blocos do documento com a Ruzit e mantém o
	índice part -> bloco (para picking e destaque).

	Os parts são regenerados apenas quando o documento muda (`rebuild`); mover
	um bloco atualiza só a part correspondente (`movePart`), o que mantém o
	arrastar suave.
]]

local Scene = {}
Scene.__index = Scene

local Renderable = import("Renderable")
local Primitives = import("Primitives")
local GPU = import("GPU")

local Vector = Primitives.Vector
local CFrame = Primitives.CFrame
local Color3 = Primitives.Color3

local OUTLINE_IDLE = Color3.new(1, 1, 1)
local OUTLINE_ACTIVE = Color3.new(1.0, 0.82, 0.25)

function Scene.new()
	local self = setmetatable({}, Scene)
	self.parts = {}      -- [índice do bloco] = BasePart
	self.owner = {}      -- BasePart -> índice
	self.selection = nil -- índice selecionado
	self.outline = Renderable.BasePart("Cube")
	self.outline.Color = OUTLINE_IDLE
	self.outline.CastShadow = false
	self.outline.ReceiveShadow = false
	self.outline.Lit = false
	self.outline.Render = false
	self.outline.IgnoreInRaycast = true
	self.outlinePart = self.outline
	return self
end

function Scene:clear()
	for _, part in pairs(self.parts) do
		pcall(function()
			part:Destroy()
		end)
	end
	self.parts = {}
	self.owner = {}
	self.selection = nil
	self.outline.Render = false
end

local function colorOf(block)
	local rgb = block.color or { 160, 160, 160 }
	return Color3.new(rgb[1] / 255, rgb[2] / 255, rgb[3] / 255)
end

--- Reconstrói todas as parts a partir do array de blocos.
function Scene:rebuild(blocks)
	self:clear()
	for index, block in ipairs(blocks) do
		local part = Renderable.BasePart("Cube")
		part.Size = Vector.new(block.size[1], block.size[2], block.size[3])
		part.CFrame = CFrame.new(
			Vector.new(block.position[1], block.position[2], block.position[3]),
			Vector.new(0, 0, 0)
		)
		part.Color = colorOf(block)
		part.CastShadow = true
		part.ReceiveShadow = true
		part.Lit = block.material ~= "neon"
		self.parts[index] = part
		self.owner[part] = index
	end
	self:refreshHighlight()
end

--- Atualiza apenas a part de um bloco (usado durante o arrastar).
function Scene:movePart(index, block)
	local part = self.parts[index]
	if not part then
		return
	end
	part.Size = Vector.new(block.size[1], block.size[2], block.size[3])
	part.CFrame = CFrame.new(
		Vector.new(block.position[1], block.position[2], block.position[3]),
		Vector.new(0, 0, 0)
	)
	part.Color = colorOf(block)
	self:refreshHighlight()
end

function Scene:setSelection(index)
	self.selection = index
	self:refreshHighlight()
end

--- Destaque: contorno um pouco maior que o bloco selecionado.
function Scene:setVisible(on)
	self.hidden = not on
	for _, part in pairs(self.parts) do
		part.Render = on
	end
	self.outline.Render = on and (self.selection ~= nil)
end

function Scene:refreshHighlight()
	local index = self.selection
	local block = index and self._blocks and self._blocks[index]
	if not block or self.hidden then
		self.outline.Render = false
		return
	end
	self.outline.Render = true
	self.outline.Size = Vector.new(block.size[1] + 0.06, block.size[2] + 0.06, block.size[3] + 0.06)
	self.outline.CFrame = CFrame.new(
		Vector.new(block.position[1], block.position[2], block.position[3]),
		Vector.new(0, 0, 0)
	)
	self.outline.Color = OUTLINE_ACTIVE
	self.outline.Transparency = 0.55
end

--- Guarda o array atual para o destaque consultar.
function Scene:bind(blocks)
	self._blocks = blocks
end

--- Raycast do centro da tela. Devolve (índice, posição, normal) ou nil.
function Scene:pick(origin, direction, maxDistance)
	local hit = GPU.Raycast(origin, direction, function(part)
		return self.owner[part] ~= nil and part ~= self.outline
	end, maxDistance or 1500)
	if not hit then
		return nil
	end
	local index = self.owner[hit.Part]
	if not index then
		return nil
	end
	return index, hit.Position, hit.Normal
end

function Scene:Destroy()
	self:clear()
	pcall(function()
		self.outline:Destroy()
	end)
end

return Scene
