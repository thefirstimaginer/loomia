--[[
	Céu do mapa no Studio (editor e playtest).

	Igual ao do cliente: um shader de fragmento com gradiente zênite → horizonte.
	A engine limpa a tela em preto; sem isto o viewport fica sem fundo.
	Os parâmetros seguem as diretivas `@ruzit param` de
	`assets/shaders/sky.wgsl` (ordem = slot).
]]

local Sky = {}

local Asset = import("Asset")
local LightingService = import("LightingService")

local shader = nil
local handle = nil

function Sky.ensure()
	if shader then
		return true
	end
	local ok, loaded = pcall(function()
		return Asset.GetAsset("Fragment", "shaders/sky.wgsl")
	end)
	if not ok then
		return false
	end
	shader = loaded
	return true
end

--- Aplica a cor do céu de um mapa. Devolve true se conseguiu.
function Sky.apply(world)
	if not Sky.ensure() then
		return false
	end
	local rgb = (world and (world.skyColor or world.sky_color)) or { 150, 200, 240 }
	local ok, newHandle = pcall(function()
		return LightingService.SetSkybox(shader)
	end)
	if not ok or not newHandle then
		return false
	end
	handle = newHandle
	pcall(function()
		handle:SetData("zenith_r", rgb[1] / 255)
		handle:SetData("zenith_g", rgb[2] / 255)
		handle:SetData("zenith_b", rgb[3] / 255)
		handle:SetData("horizon_r", math.min(1, rgb[1] / 255 + 0.38))
		handle:SetData("horizon_g", math.min(1, rgb[2] / 255 + 0.30))
		handle:SetData("horizon_b", math.min(1, rgb[3] / 255 + 0.18))
		handle:SetData("horizon_height", 0.15)
	end)
	return true
end

function Sky.shader()
	return shader
end

return Sky
