--[[
	Paleta do editor: materiais e cores no vocabulário do Loomia
	(`Material` em `loomia-shared/src/world.rs`).
]]

local Palette = {}

Palette.materials = {
	{ id = "plastic", label = "Plástico" },
	{ id = "grass", label = "Grama" },
	{ id = "wood", label = "Madeira" },
	{ id = "brick", label = "Tijolo" },
	{ id = "stone", label = "Pedra" },
	{ id = "neon", label = "Neon" },
}

Palette.colors = {
	{ label = "Vermelho", rgb = { 200, 60, 50 } },
	{ label = "Laranja", rgb = { 230, 140, 50 } },
	{ label = "Amarelo", rgb = { 240, 200, 70 } },
	{ label = "Verde", rgb = { 104, 160, 60 } },
	{ label = "Verde-escuro", rgb = { 78, 128, 45 } },
	{ label = "Azul", rgb = { 60, 110, 200 } },
	{ label = "Neon", rgb = { 90, 230, 255 } },
	{ label = "Branco", rgb = { 235, 235, 235 } },
	{ label = "Cinza", rgb = { 130, 130, 140 } },
	{ label = "Madeira", rgb = { 124, 86, 48 } },
	{ label = "Tijolo", rgb = { 150, 70, 55 } },
	{ label = "Escuro", rgb = { 40, 40, 46 } },
}

Palette.defaultColorIndex = 4 -- verde
Palette.defaultMaterialIndex = 1 -- plástico

return Palette
