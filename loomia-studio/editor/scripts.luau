--[[
	Scripts Luau do Studio.

	O mapão `.loom` pode carregar scripts. Como o formato oficial (loomia-shared)
	só conhece `blocks`, os scripts viajam num arquivo **companheiro**:

		maps/<id>.loom       -> mapa (JSON do WorldMap)
		maps/<id>.scripts    -> JSON { files = [ { name, source } ], main = "main" }

	O Studio lê esse arquivo ao carregar o mapa e o cliente de teste o executa
	no runtime da engine (um arquivo por script). No cliente real ele será
	enviado pelo servidor — por enquanto é local do Studio.
]]

local Scripts = {}
Scripts.__index = Scripts

local IO = import("IO")
local Serde = import("Serde")

Scripts.TEMPLATE = [[
-- Script de mapa gerado pelo Loomia Studio.
-- Roda uma vez quando o teste inicia e depois a cada Heartbeat.
local Process = import("Process")
local Renderable = import("Renderable")
local Primitives = import("Primitives")
local Players = import("Players")

local Vector = Primitives.Vector
local Color3 = Primitives.Color3

local previous = nil

Process.Heartbeat:Connect(function(dt)
	local world = Players.LocalPlayer
	if not world then
		return
	end
	local pos = world.Position
	if previous then
		local dx, dz = pos.X - previous.X, pos.Z - previous.Z
		if math.sqrt(dx * dx + dz * dz) > 0.4 then
			-- O jogador está se movendo: este é o lugar para a lógica do mapa.
		end
	end
	previous = pos
end)
]]

function Scripts.new()
	local self = setmetatable({}, Scripts)
	self.files = { { name = "main", source = Scripts.TEMPLATE } }
	self.active = "main"
	return self
end

function Scripts:names()
	local out = {}
	for index, file in ipairs(self.files) do
		out[#out + 1] = file and file.name or ("script" .. index)
	end
	return out
end

function Scripts:find(name)
	for index, file in ipairs(self.files) do
		if file.name == name then
			return file, index
		end
	end
	return nil
end

function Scripts:get(name)
	local file = self:find(name or self.active)
	return file and file.source or ""
end

--- Cria ou substitui o conteúdo de um script.
function Scripts:set(name, source)
	local file = self:find(name)
	if file then
		file.source = source
	else
		self.files[#self.files + 1] = { name = name, source = source }
	end
end

function Scripts:remove(name)
	local file, index = self:find(name)
	if index and #self.files > 1 then
		table.remove(self.files, index)
		if self.active == name then
			self.active = self.files[1].name
		end
		return file ~= nil
	end
	return false
end

--- Caminho do arquivo companheiro de um mapa.
function Scripts.pathFor(loomPath)
	return (loomPath:gsub("%.loom$", "") .. ".scripts")
end

function Scripts:toJson()
	local encoded, text = pcall(function()
		return Serde.Encode("json", { main = self.active, files = self.files }, true)
	end)
	if not encoded then
		return nil, tostring(text)
	end
	return text
end

function Scripts:save(loomPath)
	local text, err = self:toJson()
	if not text then
		return false, err
	end
	local ok, writeErr = pcall(function()
		IO.write(Scripts.pathFor(loomPath), text)
	end)
	if not ok then
		return false, tostring(writeErr)
	end
	return true
end

function Scripts.load(loomPath)
	local path = Scripts.pathFor(loomPath)
	local ok, text = pcall(function()
		return IO.read(path)
	end)
	if not ok then
		return Scripts.new()
	end
	local decoded, data = pcall(function()
		return Serde.Decode("json", text)
	end)
	local scripts = Scripts.new()
	if not decoded or type(data) ~= "table" or type(data.files) ~= "table" or #data.files == 0 then
		return scripts
	end
	scripts.files = {}
	for _, file in ipairs(data.files) do
		if type(file) == "table" and type(file.name) == "string" and type(file.source) == "string" then
			scripts.files[#scripts.files + 1] = { name = file.name, source = file.source }
		end
	end
	if #scripts.files == 0 then
		return Scripts.new()
	end
	scripts.active = data.main or scripts.files[1].name
	return scripts
end

return Scripts
