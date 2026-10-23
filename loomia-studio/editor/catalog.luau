--[[
	Catálogo de blocos prontos do Studio (25 "presets").

	Cada preset é um conjunto de blocos já posicionados em relação a uma
	origem (canto inferior central). Ao usar, o Studio instala os blocos no
	ponto apontado pelo cursor (ver `Palette` e `Main.luau`).

	Campos:
		name      rótulo exibido
		category  agrupamento na UI
		preview   cor do ícone (RGB)
		tags      palavras-chave para busca (Ctrl+P)
		blocks    lista de { at = {lx, ly, lz} local, size = {x,y,z},
		                    color = {r,g,b}, material = id, collidable = bool }
]]

local Catalog = {}

local G = { 104, 160, 60 }    -- grama
local DG = { 78, 128, 45 }    -- grama escura
local BR = { 150, 70, 55 }    -- tijolo
local WD = { 124, 86, 48 }    -- madeira
local ST = { 130, 130, 140 }  -- pedra
local TR = { 92, 64, 38 }     -- tronco
local LF = { 58, 120, 52 }    -- folhas
local RD = { 200, 60, 50 }    -- vermelho
local YL = { 240, 200, 70 }   -- amarelo
local BL = { 60, 110, 200 }   -- azul
local NEO = { 90, 230, 255 }  -- neon
local WH = { 235, 235, 235 }  -- branco
local GY = { 90, 92, 100 }    -- cinza
local DK = { 45, 47, 55 }     -- escuro

local function piece(at, size, color, material, collidable)
	return {
		at = { at[1], at[2], at[3] },
		size = { size[1], size[2], size[3] },
		color = { color[1], color[2], color[3] },
		material = material or "plastic",
		collidable = collidable ~= false,
	}
end

--- Cubo 1x1x1 (atalho usado em quase todo preset).
local function cube(level, dx, dz, color, material, opts)
	local out = piece({ (dx or 0) + 0.5, (level or 0) + 0.5, (dz or 0) + 0.5 }, { 1, 1, 1 }, color, material)
	if opts and opts.collidable == false then
		out.collidable = false
	end
	return out
end

local presets = {}

local function preset(name, category, preview, tags, build)
	presets[#presets + 1] = {
		name = name,
		category = category,
		preview = preview,
		tags = tags,
		build = build,
	}
end

-- ─── Estruturas ─────────────────────────────────────────────────────────────

preset("Baseplate 1x1", "Base", G, "base chao grama piso", function()
	return { cube(0, 0, 0, G, "grass") }
end)

preset("Plataforma 4x4", "Base", G, "plataforma piso chao", function()
	local out = {}
	for x = 0, 3 do
		for z = 0, 3 do
			out[#out + 1] = cube(0, x, z, G, "grass")
		end
	end
	return out
end)

preset("Pilar 1x4", "Estrutura", ST, "pilar coluna poste", function()
	local out = {}
	for i = 0, 3 do
		out[#out + 1] = cube(i, 0, 0, ST, "stone")
	end
	return out
end)

preset("Parede 4x3", "Estrutura", BR, "parede muro tijolo", function()
	local out = {}
	for x = 0, 3 do
		for y = 0, 2 do
			out[#out + 1] = cube(y, x, 0, BR, "brick")
		end
	end
	return out
end)

preset("Parede 4x3 (janela)", "Estrutura", BR, "parede janela vidro", function()
	local out = {}
	for x = 0, 3 do
		for y = 0, 2 do
			local isWindow = x >= 1 and x <= 2 and y == 1
			if isWindow then
				out[#out + 1] = cube(y, x, 0, BL, "plastic", { collidable = false })
			else
				out[#out + 1] = cube(y, x, 0, BR, "brick")
			end
		end
	end
	return out
end)

preset("Arco 3x3", "Estrutura", ST, "arco porta passagem", function()
	local out = {}
	out[#out + 1] = cube(0, 0, 0, ST, "stone")
	out[#out + 1] = cube(1, 0, 0, ST, "stone")
	out[#out + 1] = cube(2, 0, 0, ST, "stone")
	out[#out + 1] = cube(0, 2, 0, ST, "stone")
	out[#out + 1] = cube(1, 2, 0, ST, "stone")
	out[#out + 1] = cube(2, 2, 0, ST, "stone")
	out[#out + 1] = cube(1, 1, 0, ST, "stone", { collidable = false })
	return out
end)

preset("Escada 6 degraus", "Estrutura", WD, "escada rampa degrau", function()
	local out = {}
	for i = 0, 5 do
		out[#out + 1] = cube(i, 0, i, WD, "wood")
	end
	return out
end)

preset("Telhado 3x3", "Estrutura", RD, "telhado casa cumeeira", function()
	local out = {}
	for x = 0, 2 do
		for z = 0, 2 do
			local height = 3 + math.min(x, 2 - x)
			out[#out + 1] = cube(height, x, z, RD, "brick")
		end
	end
	return out
end)

preset("Casa 4x4", "Estrutura", BR, "casa cabana abrigo", function()
	local out = {}
	-- Piso
	for x = 0, 3 do
		for z = 0, 3 do
			out[#out + 1] = piece({ x + 0.5, 0.1, z + 0.5 }, { 1, 0.2, 1 }, WD, "wood")
		end
	end
	-- Paredes (deixando a porta em x=1..2, z=0)
	for x = 0, 3 do
		for y = 1, 3 do
			local door = z == 0 and (x == 1 or x == 2) and y <= 2
			if not door then
				out[#out + 1] = cube(y - 1, x, 0, BR, "brick")
				out[#out + 1] = cube(y - 1, x, 3, BR, "brick")
			end
			out[#out + 1] = cube(y - 1, 0, x, BR, "brick")
			out[#out + 1] = cube(y - 1, 3, x, BR, "brick")
		end
	end
	-- Telhado
	for x = -1, 4 do
		for z = -1, 4 do
			local edge = (x == -1 or x == 4 or z == -1 or z == 4)
			out[#out + 1] = cube(4, x, z, edge and DK or GY, "stone")
		end
	end
	return out
end)

-- ─── Natureza ───────────────────────────────────────────────────────────────

preset("Árvore pequena", "Natureza", LF, "arvore planta tronco", function()
	local out = {}
	out[#out + 1] = piece({ 0.5, 1.5, 0.5 }, { 1, 3, 1 }, TR, "wood")
	out[#out + 1] = piece({ 0.5, 4.0, 0.5 }, { 3, 2, 3 }, LF, "grass")
	out[#out + 1] = piece({ 0.5, 5.25, 0.5 }, { 2, 0.5, 2 }, LF, "grass")
	return out
end)

preset("Árvore grande", "Natureza", LF, "arvore grande copa", function()
	local out = {}
	out[#out + 1] = piece({ 0.5, 2.5, 0.5 }, { 1, 5, 1 }, TR, "wood")
	out[#out + 1] = piece({ 0.5, 6.0, 0.5 }, { 5, 3, 5 }, LF, "grass")
	out[#out + 1] = piece({ 0.5, 7.8, 0.5 }, { 3, 1, 3 }, LF, "grass")
	return out
end)

preset("Arbusto", "Natureza", LF, "arbusto mato", function()
	local out = {}
	out[#out + 1] = cube(0, 0, 0, LF, "grass")
	out[#out + 1] = cube(0, 1, 0, LF, "grass")
	out[#out + 1] = cube(0, 0, 1, DG, "grass")
	return out
end)

preset("Rocha", "Natureza", ST, "rocha pedra monte", function()
	local out = {}
	out[#out + 1] = piece({ 0.5, 0.7, 0.5 }, { 1.6, 1.4, 1.6 }, ST, "stone")
	out[#out + 1] = piece({ 1.4, 0.35, 1.0 }, { 0.9, 0.7, 0.9 }, GY, "stone")
	return out
end)

preset("Lago 5x5", "Natureza", BL, "lago agua rio piscina", function()
	local out = {}
	for x = 0, 4 do
		for z = 0, 4 do
			local shallow = (x == 0 or x == 4 or z == 0 or z == 4)
			out[#out + 1] = piece(
				{ x + 0.5, -0.15, z + 0.5 },
				{ 1, 0.3, 1 },
				shallow and NEO or BL,
				"neon",
				false
			)
		end
	end
	return out
end)

preset("Flor neon", "Natureza", NEO, "flor neon deco planta", function()
	local out = {}
	out[#out + 1] = piece({ 0.5, 0.4, 0.5 }, { 0.15, 0.8, 0.15 }, LF, "grass", false)
	out[#out + 1] = piece({ 0.5, 0.95, 0.5 }, { 0.5, 0.5, 0.5 }, NEO, "neon", false)
	return out
end)

-- ─── Interativos / decoração ────────────────────────────────────────────────

preset("Spawn pad", "Interativo", WH, "spawn inicio nascer", function()
	local out = {}
	for dx = -1, 1 do
		for dz = -1, 1 do
			local color = ((dx + dz) % 2 == 0) and WH or RD
			out[#out + 1] = piece({ (dx + 1) * 2.0 + 1, 0.06, (dz + 1) * 2.0 + 1 }, { 2, 0.12, 2 }, color, "plastic", false)
		end
	end
	return out
end)

preset("Poste de luz", "Interativo", NEO, "poste luz lampada neon", function()
	local out = {}
	out[#out + 1] = piece({ 0.2, 1.5, 0.2 }, { 0.4, 3.0, 0.4 }, GY, "stone")
	out[#out + 1] = piece({ 0.2, 3.3, 0.2 }, { 0.8, 0.6, 0.8 }, NEO, "neon", false)
	return out
end)

preset("Checkpoint", "Interativo", YL, "checkpoint bandeira meta", function()
	local out = {}
	out[#out + 1] = piece({ 0.2, 1.5, 0.2 }, { 0.2, 3, 0.2 }, GY, "stone")
	out[#out + 1] = piece({ 1.1, 2.6, 0.2 }, { 1.8, 0.9, 0.1 }, YL, "plastic", false)
	return out
end)

preset("Placa", "Interativo", WD, "placa aviso letreiro", function()
	local out = {}
	out[#out + 1] = piece({ 0.15, 1.0, 0.15 }, { 0.15, 2, 0.15 }, WD, "wood")
	out[#out + 1] = piece({ 1.2, 1.7, 0.15 }, { 2.0, 1.0, 0.15 }, WD, "wood", false)
	return out
end)

preset("Cerca 4", "Interativo", WD, "cerca grade muro", function()
	local out = {}
	for x = 0, 3 do
		out[#out + 1] = piece({ x + 0.5, 0.5, 0.15 }, { 0.2, 1.0, 0.2 }, WD, "wood")
	end
	out[#out + 1] = piece({ 2.0, 0.85, 0.15 }, { 4.0, 0.15, 0.1 }, WD, "wood", false)
	out[#out + 1] = piece({ 2.0, 0.45, 0.15 }, { 4.0, 0.15, 0.1 }, WD, "wood", false)
	return out
end)

preset("Dominoes", "Decoração", RD, "dominó peças torre", function()
	return {
		piece({ 0.5, 1.0, 0.5 }, { 1, 2, 1 }, RD, "plastic"),
		piece({ 2.0, 2.0, 1.0 }, { 1, 4, 1 }, YL, "plastic"),
		piece({ 3.5, 1.5, 1.5 }, { 1, 3, 1 }, BL, "plastic"),
	}
end)

-- ─── Peças simples ──────────────────────────────────────────────────────────

preset("Cubo", "Simples", GY, "cubo bloco", function()
	return { cube(0, 0, 0, GY, "plastic") }
end)

preset("Esfera", "Simples", BL, "esfera bola", function()
	return { piece({ 0.5, 0.7, 0.5 }, { 1.4, 1.4, 1.4 }, BL, "plastic") }
end)

preset("Rampa", "Simples", GY, "rampa inclinada", function()
	return {
		piece({ 1.0, 0.5, 1.0 }, { 2, 1, 2 }, GY, "stone"),
		piece({ 2.2, 1.0, 1.0 }, { 0.6, 2, 2 }, ST, "stone"),
	}
end)

preset("Teto plano 4x4", "Simples", GY, "teto laje cobertura", function()
	local out = {}
	for x = 0, 3 do
		for z = 0, 3 do
			out[#out + 1] = piece({ x + 0.5, 0.1, z + 0.5 }, { 1, 0.2, 1 }, GY, "stone")
		end
	end
	return out
end)

--- Monta os blocos de um preset já posicionados no mundo.
--- `origin` = canto inferior central (x, y, z) de onde o preset cresce.
function Catalog.instantiate(presetName, origin)
	local entry
	for _, candidate in ipairs(presets) do
		if candidate.name == presetName then
			entry = candidate
			break
		end
	end
	if not entry then
		return nil
	end
	local blocks = entry.build()
	-- Centraliza no eixo X e fixa no eixo Y/Z a partir da origem.
	local minX, maxX, minZ, maxZ = math.huge, -math.huge, math.huge, -math.huge
	for _, block in ipairs(blocks) do
		minX = math.min(minX, block.at[1])
		maxX = math.max(maxX, block.at[1])
		minZ = math.min(minZ, block.at[3])
		maxZ = math.max(maxZ, block.at[3])
	end
	local cx = (minX + maxX) * 0.5
	local cz = (minZ + maxZ) * 0.5
	for _, block in ipairs(blocks) do
		block.position = {
			origin[1] + block.at[1] - cx,
			origin[2] + block.at[2],
			origin[3] + block.at[3] - cz,
		}
		block.at = nil
		block.build = nil
	end
	return blocks, entry
end

function Catalog.list()
	local out = {}
	for _, entry in ipairs(presets) do
		out[#out + 1] = entry
	end
	return out
end

--- Busca por nome/categoria/tags.
function Catalog.search(query)
	query = (query or ""):lower()
	local out = {}
	for _, entry in ipairs(presets) do
		local haystack = (entry.name .. " " .. entry.category .. " " .. (entry.tags or "")):lower()
		if haystack:find(query, 1, true) then
			out[#out + 1] = entry
		end
	end
	return out
end

return Catalog
