--[[
	Interface do Studio (2D, `import("GUI")`).

	Layout (1280x720 padrão, hud escalado pela altura real):
		topo:   título, status e atalhos
		direita: paleta (cores/materiais), tamanho do bloco, dados do mapa
		baixo:  ferramentas e ajuda
		centro: retículo do viewport

	O texto usa a fonte embutida da engine (sem assets externos).
]]

local UI = {}
UI.__index = UI

local GUI = import("GUI")
local Primitives = import("Primitives")

local Dim = Primitives.Dim.new
local Color3 = Primitives.Color3
local Asset = import("Asset")

local fontCache = nil

--- Fonte do Studio (a engine não tem fonte embutida).
function UI.font()
	if not fontCache then
		fontCache = Asset.GetAsset("Font", "fonts/DejaVuSans.ttf")
	end
	return fontCache
end


function UI.new(width, height)
	local self = setmetatable({}, UI)
	self.width = width
	self.height = height
	self.primitives = {}
	return self
end

function UI:track(primitive)
	self.primitives[#self.primitives + 1] = primitive
	return primitive
end

function UI.rect(x, y, w, h, color, transparency)
	local square = GUI.Basic.Square()
	square.Size = Dim(w, h)
	square.Position = Dim(x, y)
	square.Color = color or Color3.new(0.07, 0.08, 0.11)
	square.Transparency = transparency or 0.15
	return square
end

function UI.text(text, size, x, y, color)
	local primitive = GUI.Basic.Font(UI.font())
	primitive.Text = text or ""
	primitive.TextSize = size or 14
	primitive.Position = Dim(x, y)
	primitive.Size = Dim(0, (size or 14) + 4)
	primitive.Color = color or Color3.new(0.92, 0.94, 0.97)
	return primitive
end

function UI.color(rgb)
	return Color3.new(rgb[1] / 255, rgb[2] / 255, rgb[3] / 255)
end

--- Cria a UI estática. Devolve a própria UI (com referências aos textos).
function UI:build(palette, materials)
	self:track(UI.rect(0, 0, self.width, 44, Color3.new(0.07, 0.08, 0.11), 0.05))
	self:track(UI.text("LOOMIA STUDIO", 18, 16, 12, Color3.new(0.42, 0.84, 1.0)))
	self.status = self:track(UI.text("carregando…", 14, 176, 15, Color3.new(0.62, 0.66, 0.74)))
	self.hint = self:track(UI.text("", 13, 420, 16, Color3.new(1.0, 0.78, 0.35)))

	-- Painel direito.
	local px = self.width - 264
	self:track(UI.rect(px, 56, 248, self.height - 112, Color3.new(0.07, 0.08, 0.11), 0.12))

	self:track(UI.text("COR", 12, px + 16, 70, Color3.new(0.62, 0.66, 0.74)))
	self.swatches = {}
	for index, entry in ipairs(palette.colors) do
		local column = (index - 1) % 6
		local row = math.floor((index - 1) / 6)
		local x, y = px + 16 + column * 36, 90 + row * 34
		local swatch = self:track(UI.rect(x, y, 30, 28, UI.color(entry.rgb), 0))
		self.swatches[index] = swatch
	end

	self:track(UI.text("MATERIAL", 12, px + 16, 178, Color3.new(0.62, 0.66, 0.74)))
	self.materialLabels = {}
	for index, entry in ipairs(materials) do
		local y = 198 + (index - 1) * 22
		local label = self:track(UI.text(entry.label, 14, px + 16, y, Color3.new(0.62, 0.66, 0.74)))
		self.materialLabels[index] = label
	end

	self:track(UI.text("TAMANHO", 12, px + 16, 344, Color3.new(0.62, 0.66, 0.74)))
	self:track(UI.text("( [ e ] tamanho   -/+ altura )", 12, px + 16, 360, Color3.new(0.62, 0.66, 0.74)))
	self.sizeLabel = self:track(UI.text("1 x 1 x 1", 14, px + 16, 380, Color3.new(0.92, 0.94, 0.97)))

	self:track(UI.text("MAPA", 12, px + 16, 424, Color3.new(0.62, 0.66, 0.74)))
	self.mapLabel = self:track(UI.text("", 14, px + 16, 442, Color3.new(0.92, 0.94, 0.97)))
	self.countLabel = self:track(UI.text("", 14, px + 16, 462, Color3.new(0.92, 0.94, 0.97)))
	self.spawnLabel = self:track(UI.text("", 14, px + 16, 482, Color3.new(0.92, 0.94, 0.97)))

	-- Barra inferior.
	local by = self.height - 44
	self:track(UI.rect(0, by, self.width, 44, Color3.new(0.07, 0.08, 0.11), 0.05))
	self:track(UI.text(
		"Clique: colocar   |   Shift+Clique: apagar   |   Alt+Clique: pintar   |   RMB: orbitar   |   MMB: mover   |   [ ]: tamanho   |   F: focar   |   G: grade   |   Ctrl+Z/Y: desfazer/refazer   |   Ctrl+S: salvar   |   Ctrl+L: recarregar",
		12,
		16,
		by + 14,
		Color3.new(0.62, 0.66, 0.74)
	))

	-- Retículo.
	self.reticleH = self:track(UI.rect(self.width * 0.5 - 7, self.height * 0.5 - 1, 14, 2, Color3.new(0.92, 0.94, 0.97), 0.4))
	self.reticleV = self:track(UI.rect(self.width * 0.5 - 1, self.height * 0.5 - 7, 2, 14, Color3.new(0.92, 0.94, 0.97), 0.4))

	return self
end

function UI:setStatus(text, color)
	self.status.Text = text
	self.status.Color = color or Color3.new(0.62, 0.66, 0.74)
end

function UI:setHint(text)
	self.hint.Text = text or ""
end

function UI:selectColor(index)
	for i, swatch in ipairs(self.swatches) do
		swatch.Size = Dim(i == index and 34 or 30, i == index and 32 or 28)
		swatch.Position = Dim(
			swatch.Position.X - (i == index and 2 or 0),
			swatch.Position.Y - (i == index and 2 or 0)
		)
	end
end

function UI:selectMaterial(index)
	for i, label in ipairs(self.materialLabels) do
		label.Color = i == index and Color3.new(0.42, 0.84, 1.0) or Color3.new(0.62, 0.66, 0.74)
	end
end

function UI:setSize(size)
	-- `Dim` só aceita outro Dim; para largura livre o texto usa 0 (auto).
	self.sizeLabel.Text = string.format("%d x %d x %d", size[1], size[2], size[3])
	self.sizeLabel.Size = Dim(0, 20)
end

function UI:setMapInfo(world, count, spawn)
	self.mapLabel.Text = tostring(world.name or world.id or "?")
	self.countLabel.Text = string.format("%d blocos%s", count, world.dirty and " (não salvo)" or "")
	self.spawnLabel.Text = string.format("spawn %.1f, %.1f, %.1f", spawn[1], spawn[2], spawn[3])
end

function UI:Destroy()
	for _, primitive in ipairs(self.primitives) do
		pcall(function()
			primitive:Destroy()
		end)
	end
	self.primitives = {}
end


-- Cores de estado expostas para o `Main.luau`.
UI.OK = Color3.new(0.55, 0.9, 0.6)
UI.WARN = Color3.new(1.0, 0.78, 0.35)
UI.MUTED = Color3.new(0.62, 0.66, 0.74)
UI.TEXT = Color3.new(0.92, 0.94, 0.97)
UI.ACCENT = Color3.new(0.42, 0.84, 1.0)
UI.PANEL_SOFT = Color3.new(0.13, 0.14, 0.18)

return UI
