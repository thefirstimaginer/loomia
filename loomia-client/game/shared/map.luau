--[[
	Renderiza um `WorldMap` (o `.loom` vindo do servidor) com a Ruzit.

	O cliente NÃO edita o mapa: ele só lê o JSON recebido em `Welcome` (ou pelo
	`GET /api/maps/{id}`) e cria as `Renderable.BasePart` correspondentes.

	Otimização: blocos de 1x1 no plano que compartilham altura, espessura,
	material e cor são fundidos numa única part maior (greedy meshing em X/Z).
	O mapa "baseplate" passa de ~80 parts para poucas dezenas — importante,
	porque cada part é um draw call.
]]

local Map = {}
Map.__index = Map

local Renderable = import("Renderable")
local Primitives = import("Primitives")

local Vector = Primitives.Vector
local CFrame = Primitives.CFrame
local Color3 = Primitives.Color3

--- Chave de agrupamento: blocos com mesmo centro Y, espessura, material, cor e
--- colisão podem ser fundidos horizontalmente.
local function cellKey(block)
	local pos, size = block.position, block.size
	return table.concat({
		string.format("%.3f", pos[2]),
		string.format("%.3f", size[2]),
		tostring(block.material),
		table.concat(block.color or { 160, 160, 160 }, ","),
		tostring(block.collidable ~= false),
	}, "|")
end

--- Bloco 1x1x(qualquer) alinhado à grade no plano XZ?
local function isUnitCell(block)
	local pos, size = block.position, block.size
	if math.abs(size[1] - 1.0) > 1e-3 or math.abs(size[3] - 1.0) > 1e-3 then
		return false
	end
	local gx = pos[1] - math.floor(pos[1]) - 0.5
	local gz = pos[3] - math.floor(pos[3]) - 0.5
	return math.abs(gx) < 1e-3 and math.abs(gz) < 1e-3
end

function Map.new(world)
	local self = setmetatable({}, Map)
	self.world = world
	self.parts = {}
	self.count = 0
	self.minY = 0
	self.maxY = 0
	return self
end

function Map:Destroy()
	for _, part in ipairs(self.parts) do
		pcall(function()
			part:Destroy()
		end)
	end
	self.parts = {}
	self.count = 0
end

--- Cor do céu do mapa (aceita `sky_color` e `skyColor`).
function Map.skyColor(world)
	local rgb = (world and (world.skyColor or world.sky_color)) or { 150, 200, 240 }
	return Color3.new(rgb[1] / 255, rgb[2] / 255, rgb[3] / 255)
end

--- Aplica o céu do mapa num shader de cena (gradiente zênite → horizonte).
--- A engine limpa a tela em preto; sem skybox o fundo fica preto.
Map.skyShader = nil
Map.skyHandle = nil

function Map.applySky(world)
	local LightingService = import("LightingService")
	local Asset = import("Asset")
	local rgb = (world and (world.skyColor or world.sky_color)) or { 150, 200, 240 }
	if not Map.skyShader then
		local ok, shader = pcall(function()
			return Asset.GetAsset("Fragment", "shaders/sky.wgsl")
		end)
		if not ok then
			return false
		end
		Map.skyShader = shader
	end
	local handle = LightingService.SetSkybox(Map.skyShader)
	Map.skyHandle = handle
	-- Slots na ordem das diretivas `@ruzit param` do shader.
	local zenith = { rgb[1] / 255, rgb[2] / 255, rgb[3] / 255 }
	local horizon = {
		math.min(1, zenith[1] + 0.38),
		math.min(1, zenith[2] + 0.30),
		math.min(1, zenith[3] + 0.18),
	}
	local values = {
		zenith_r = zenith[1],
		zenith_g = zenith[2],
		zenith_b = zenith[3],
		horizon_r = horizon[1],
		horizon_g = horizon[2],
		horizon_b = horizon[3],
		horizon_height = 0.15,
	}
	for name, value in pairs(values) do
		pcall(function()
			handle:SetData(name, value)
		end)
	end
	return true
end

local function spawnPart(parts, position, size, r, g, b)
	local part = Renderable.BasePart("Cube")
	part.Size = Vector.new(size[1], size[2], size[3])
	part.CFrame = CFrame.new(Vector.new(position[1], position[2], position[3]), Vector.new(0, 0, 0))
	part.Color = Color3.new(r, g, b)
	part.CastShadow = true
	part.ReceiveShadow = true
	part.Lit = true
	parts[#parts + 1] = part
	return part
end

--- Mescla uma coluna (células em Z) em retângulos contínuos: { {z0, z1}, ... }.
local function runLengths(column)
	local cells = {}
	for gz in pairs(column) do
		cells[#cells + 1] = gz
	end
	table.sort(cells)
	local runs = {}
	local i = 1
	while i <= #cells do
		local startZ, endZ = cells[i], cells[i]
		while i < #cells and cells[i + 1] == endZ + 1 do
			i = i + 1
			endZ = cells[i]
		end
		runs[#runs + 1] = { startZ, endZ }
		i = i + 1
	end
	return runs
end

--- Cria os parts do mundo. Devolve a própria `Map`.
function Map:build()
	self:Destroy()
	local blocks = (self.world and self.world.blocks) or {}
	local minY, maxY = math.huge, -math.huge

	local groups = {}
	local order = {}
	local singles = {}

	for _, block in ipairs(blocks) do
		local pos, size = block.position, block.size
		if pos and size then
			local half = size[2] * 0.5
			minY = math.min(minY, pos[2] - half)
			maxY = math.max(maxY, pos[2] + half)
			if isUnitCell(block) then
				local key = cellKey(block)
				local group = groups[key]
				if not group then
					group = { cells = {}, block = block }
					groups[key] = group
					order[#order + 1] = key
				end
				local gx, gz = math.floor(pos[1] + 0.5), math.floor(pos[3] + 0.5)
				local column = group.cells[gx] or {}
				group.cells[gx] = column
				column[gz] = true
			else
				singles[#singles + 1] = block
			end
		end
	end

	for _, key in ipairs(order) do
		local group = groups[key]
		local template = group.block
		local rgb = template.color or { 160, 160, 160 }
		local r, g, b = rgb[1] / 255, rgb[2] / 255, rgb[3] / 255
		local y = template.position[2]
		local thickness = template.size[2]

		-- Runs por coluna X, processando as colunas em ordem crescente.
		local columns = {}
		local xs = {}
		for gx, column in pairs(group.cells) do
			columns[gx] = runLengths(column)
			xs[#xs + 1] = gx
		end
		table.sort(xs)

		-- Expande cada run em X enquanto a coluna vizinha tiver o mesmo run.
		local pending = {}
		for _, gx in ipairs(xs) do
			local seen = {}
			for _, run in ipairs(columns[gx]) do
				local runKey = run[1] .. ":" .. run[2]
				seen[runKey] = true
				local active = pending[runKey]
				if active and active.lastX == gx - 1 then
					active.lastX = gx
				else
					if active then
						spawnPart(self.parts, {
							(active.firstX + active.lastX) * 0.5,
							y,
							(active.startZ + active.endZ) * 0.5,
						}, { active.lastX - active.firstX + 1, thickness, active.endZ - active.startZ + 1 }, r, g, b)
					end
					pending[runKey] = { firstX = gx, lastX = gx, startZ = run[1], endZ = run[2] }
				end
			end
			-- Runs desta coluna que não continuam precisam ser fechados agora.
			for runKey, active in pairs(pending) do
				if not seen[runKey] then
					spawnPart(self.parts, {
						(active.firstX + active.lastX) * 0.5,
						y,
						(active.startZ + active.endZ) * 0.5,
					}, { active.lastX - active.firstX + 1, thickness, active.endZ - active.startZ + 1 }, r, g, b)
					pending[runKey] = nil
				end
			end
		end
		for _, active in pairs(pending) do
			spawnPart(self.parts, {
				(active.firstX + active.lastX) * 0.5,
				y,
				(active.startZ + active.endZ) * 0.5,
			}, { active.lastX - active.firstX + 1, thickness, active.endZ - active.startZ + 1 }, r, g, b)
		end
	end

	-- Parts avulsas: paredes, rampas, escadas, decoração com tamanhos próprios.
	for _, block in ipairs(singles) do
		local rgb = block.color or { 160, 160, 160 }
		spawnPart(self.parts, block.position, block.size, rgb[1] / 255, rgb[2] / 255, rgb[3] / 255)
	end

	self.count = #self.parts
	self.minY = (minY == math.huge) and 0 or minY
	self.maxY = (maxY == -math.huge) and 0 or maxY
	return self
end

return Map
