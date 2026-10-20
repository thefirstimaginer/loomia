--[[
	Documento do mapa: o modelo de dados editável (igual ao `WorldMap` do
	crate `loomia-shared`, serializado como `.loom`).

	Um bloco é:
		{ position = {x,y,z}, size = {x,y,z}, color = {r,g,b},
		  material = "plastic"|"grass"|..., collidable = boolean }

	O documento guarda o histórico de undo/redo (snapshots baratos do array de
	blocos) e sabe salvar localmente (`IO.write`) e/ou no servidor.
]]

local Document = {}
Document.__index = Document

local Serde = import("Serde")
local IO = import("IO")

Document.DEFAULT_SKY = { 150, 200, 240 }

--- Mapa vazio, mas jogável: baseplate 80x80 com o topo em y = 0.
function Document.empty(id, name)
	return {
		id = id or "baseplate",
		name = name or "Baseplate Clássica",
		spawn = { 0, 0.2, 4 },
		skyColor = Document.DEFAULT_SKY,
		blocks = {
			{ position = { 0, -0.5, 0 }, size = { 80, 1, 80 }, color = { 104, 160, 60 }, material = "grass", collidable = true },
		},
	}
end

function Document.new(world)
	local self = setmetatable({}, Document)
	self.world = world or Document.empty()
	self.undoStack = {}
	self.redoStack = {}
	self.dirty = false
	self.path = nil
	return self
end

function Document:blocks()
	return self.world.blocks
end

--- Snapshot antes de uma modificação (barato: só valores primitivos).
function Document:pushUndo()
	self.undoStack[#self.undoStack + 1] = Serde.Decode("json", Serde.Encode("json", self.world.blocks))
	if #self.undoStack > 128 then
		table.remove(self.undoStack, 1)
	end
	self.redoStack = {}
	self.dirty = true
end

function Document:undo()
	local previous = table.remove(self.undoStack)
	if not previous then
		return false
	end
	self.redoStack[#self.redoStack + 1] = self.world.blocks
	self.world.blocks = previous
	self.dirty = true
	return true
end

function Document:redo()
	local next_ = table.remove(self.redoStack)
	if not next_ then
		return false
	end
	self.undoStack[#self.undoStack + 1] = self.world.blocks
	self.world.blocks = next_
	self.dirty = true
	return true
end

function Document:addBlock(block)
	self:pushUndo()
	self.world.blocks[#self.world.blocks + 1] = block
	return #self.world.blocks
end

function Document:removeBlock(index)
	if not self.world.blocks[index] then
		return false
	end
	self:pushUndo()
	table.remove(self.world.blocks, index)
	return true
end

function Document:replaceBlock(index, block)
	if not self.world.blocks[index] then
		return false
	end
	self.world.blocks[index] = block
	self.dirty = true
	return true
end

function Document:setSpawn(x, y, z)
	self:pushUndo()
	self.world.spawn = { x, y, z }
end

function Document:setSky(rgb)
	self:pushUndo()
	self.world.skyColor = { rgb[1], rgb[2], rgb[3] }
end

--- Grava o JSON (`.loom`) num caminho local. Cria a pasta se preciso.
function Document:saveLocal(path)
	local encoded, text = pcall(function()
		return Serde.Encode("json", self.world)
	end)
	if not encoded then
		return false, tostring(text)
	end
	local ok, err = pcall(function()
		local dir = path:match("^(.*)[/\\][^/\\]+$")
		if dir then
			IO.mkdir(dir)
		end
		IO.write(path, text)
	end)
	if not ok then
		return false, tostring(err)
	end
	self.path = path
	self.dirty = false
	return true, "salvo em " .. path
end

--- Lê um `.loom` local.
function Document.loadLocal(path)
	local ok, text = pcall(function()
		return IO.read(path)
	end)
	if not ok then
		return nil, tostring(text)
	end
	local decoded, world = pcall(function()
		return Serde.Decode("json", text)
	end)
	if not decoded or type(world) ~= "table" or type(world.blocks) ~= "table" then
		return nil, "arquivo não é um mapa válido"
	end
	local document = Document.new(world)
	document.path = path
	return document, nil
end

return Document
