--[[
	Interface do Studio (2D, `import("GUI")`).

	Layout (1280x720):
		topo     título, status, botões de arquivo (NOVO, ABRIR, SALVAR, TESTAR)
		esquerda paleta de blocos prontos (25 presets, 5 por linha)
		direita  abas: INSPETOR | SCRIPTS | MAPA
		baixo    atalhos

	A engine não expõe hit-test de GUI, então cada clique é testado em Lua
	contra os retângulos registrados em `UI:click(x, y)`. O `Main.luau`
	registra os handlers com `UI:onButton`, `UI:onPreset` e `UI:onTab`.
]]

local UI = {}
UI.__index = UI

local GUI = import("GUI")
local Primitives = import("Primitives")
local Asset = import("Asset")

local Dim = Primitives.Dim.new
local Color3 = Primitives.Color3

local PANEL = Color3.new(0.07, 0.08, 0.11)
local PANEL_SOFT = Color3.new(0.13, 0.14, 0.18)
local TEXT = Color3.new(0.92, 0.94, 0.97)
local MUTED = Color3.new(0.62, 0.66, 0.74)
local ACCENT = Color3.new(0.42, 0.84, 1.0)
local WARN = Color3.new(1.0, 0.78, 0.35)
local OK = Color3.new(0.55, 0.9, 0.6)
local DANGER = Color3.new(0.95, 0.42, 0.38)

local fontCache = nil
function UI.font()
	if not fontCache then
		fontCache = Asset.GetAsset("Font", "fonts/DejaVuSans.ttf")
	end
	return fontCache
end

local function dim(w, h)
	return Dim(w or 0, h or 0)
end

function UI.new(width, height)
	local self = setmetatable({}, UI)
	self.width = width
	self.height = height
	self.primitives = {}
	self.buttons = {}
	self.presets = {}
	self.tabs = {}
	self.buttonHandlers = {}
	self.presetHandlers = {}
	self.tabHandlers = {}
	self.tab = "inspector"
	self.visible = true
	return self
end

function UI:track(primitive)
	self.primitives[#self.primitives + 1] = primitive
	return primitive
end

function UI.rect(x, y, w, h, color, transparency)
	local square = GUI.Basic.Square()
	square.Size = dim(w, h)
	square.Position = dim(x, y)
	if color then
		square.Color = color
	end
	square.Transparency = transparency or 0
	return square
end

function UI.text(content, size, x, y, color)
	local primitive = GUI.Basic.Font(UI.font())
	primitive.Text = content or ""
	primitive.TextSize = size or 14
	primitive.Position = dim(x, y)
	primitive.Size = dim(0, (size or 14) + 4)
	if color then
		primitive.Color = color
	end
	return primitive
end

function UI.color(rgb)
	return Color3.new(rgb[1] / 255, rgb[2] / 255, rgb[3] / 255)
end

-- ---------------------------------------------------------------------------
-- Construção
-- ---------------------------------------------------------------------------

function UI:build(palette, materials, presets)
	self.palette = palette
	self.materials = materials
	self.presetList = presets
	self:buildTopBar()
	self:buildLeftPanel()
	self:buildInspector()
	self:buildScriptsTab()
	self:buildMapTab()
	self:buildBottomBar()
	self:buildReticle()
	self:setTab("inspector")
	return self
end

function UI:button(id, content, x, y, w, h, color)
	local background = self:track(UI.rect(x, y, w, h, color or PANEL_SOFT, 0.05))
	local label = self:track(UI.text(content, 13, x + 10, y + math.floor((h - 17) * 0.5) + 1, TEXT))
	self.buttons[id] = { x = x, y = y, w = w, h = h, background = background, label = label }
	return self.buttons[id]
end

function UI:buildTopBar()
	self:track(UI.rect(0, 0, self.width, 46, PANEL, 0.02))
	self:track(UI.text("LOOMIA STUDIO", 18, 16, 13, ACCENT))
	self.status = self:track(UI.text("", 13, 176, 17, MUTED))

	local x = self.width - 264 - 16
	self:button("new", "NOVO", x, 8, 60, 30, Color3.new(0.2, 0.24, 0.3))
	x = x + 66
	self:button("open", "ABRIR", x, 8, 60, 30, Color3.new(0.2, 0.24, 0.3))
	x = x + 66
	self:button("save", "SALVAR", x, 8, 64, 30, Color3.new(0.16, 0.30, 0.42))
	x = x + 70
	self:button("play", "TESTAR", x, 8, 62, 30, Color3.new(0.14, 0.38, 0.22))
end

function UI:buildLeftPanel()
	local px, py = 16, 58
	self:track(UI.rect(px, py, 250, self.height - 104, PANEL, 0.10))
	self:track(UI.text("BLOCOS", 12, px + 12, py + 10, MUTED))
	self.presetFilterLabel = self:track(UI.text("", 11, px + 12, py + 28, MUTED))

	local columns, cellW, cellH = 5, 44, 42
	local startY = py + 48
	for index, entry in ipairs(self.presetList) do
		local column = (index - 1) % columns
		local row = math.floor((index - 1) / columns)
		local x = px + 10 + column * (cellW + 4)
		local y = startY + row * (cellH + 4)
		local swatch = self:track(UI.rect(x, y, 40, 38, entry.preview and UI.color(entry.preview) or PANEL_SOFT, 0.05))
		self.presets[index] = { x = x, y = y, w = 40, h = 38, swatch = swatch, entry = entry }
	end
	self.presetColumns = columns
	local gridBottom = startY + math.ceil(#self.presetList / columns) * (cellH + 4)
	self:track(UI.text("clique para selecionar o bloco", 11, px + 12, gridBottom + 8, MUTED))
end

function UI:tabButton(id, content, x, y, w)
	local background = self:track(UI.rect(x, y, w, 26, PANEL_SOFT, 0.2))
	local label = self:track(UI.text(content, 12, x + 8, y + 7, MUTED))
	self.tabs[id] = { x = x, y = y, w = w, h = 26, background = background, label = label }
end

function UI:buildInspector()
	local px = self.width - 264
	self:track(UI.rect(px, 58, 248, self.height - 104, PANEL, 0.10))
	self:tabButton("inspector", "INSPETOR", px + 8, 66, 76)
	self:tabButton("scripts", "SCRIPTS", px + 88, 66, 70)
	self:tabButton("map", "MAPA", px + 162, 66, 58)

	self.inspectorGroup = {}
	local function track(primitive)
		self.inspectorGroup[#self.inspectorGroup + 1] = primitive
		return self:track(primitive)
	end

	track(UI.text("COR", 11, px + 12, 108, MUTED))
	self.swatches = {}
	for index, entry in ipairs(self.palette.colors) do
		local column = (index - 1) % 4
		local row = math.floor((index - 1) / 4)
		local x, y = px + 12 + column * 58, 124 + row * 30
		local swatch = track(UI.rect(x, y, 52, 24, UI.color(entry.rgb), 0))
		self.swatches[index] = swatch
	end

	local materialY = 124 + math.ceil(#self.palette.colors / 4) * 30 + 10
	track(UI.text("MATERIAL", 11, px + 12, materialY, MUTED))
	self.materialLabels = {}
	for index, entry in ipairs(self.materials) do
		local column = (index - 1) % 3
		local row = math.floor((index - 1) / 3)
		local y = materialY + 16 + row * 20
		local label = track(UI.text(entry.label, 12, px + 12 + column * 76, y, MUTED))
		self.materialLabels[index] = label
	end

	local sizeY = materialY + 16 + math.ceil(#self.materials / 3) * 20 + 10
	track(UI.text("TAMANHO", 11, px + 12, sizeY, MUTED))
	track(UI.text("[ ] largura    - = altura", 11, px + 12, sizeY + 14, MUTED))
	self.sizeLabel = track(UI.text("1 x 1 x 1", 13, px + 12, sizeY + 32, TEXT))

	local selY = sizeY + 58
	track(UI.text("SELECAO", 11, px + 12, selY, MUTED))
	self.selectionLabel = track(UI.text("nenhum bloco", 12, px + 12, selY + 16, TEXT))
	self:button("deleteSel", "APAGAR", px + 12, selY + 38, 104, 26, Color3.new(0.35, 0.14, 0.14))
	self:button("dupSel", "DUPLICAR", px + 124, selY + 38, 104, 26, PANEL_SOFT)
end

function UI:buildMapTab()
	local px = self.width - 264
	self.mapGroup = {}
	local function track(primitive)
		self.mapGroup[#self.mapGroup + 1] = primitive
		return self:track(primitive)
	end
	local y = 110
	track(UI.text("NOME", 11, px + 12, y, MUTED))
	self.mapNameLabel = track(UI.text("", 13, px + 12, y + 16, TEXT))
	y = y + 44
	track(UI.text("ID (arquivo .loom)", 11, px + 12, y, MUTED))
	self.mapIdLabel = track(UI.text("", 13, px + 12, y + 16, TEXT))
	y = y + 44
	track(UI.text("SPAWN", 11, px + 12, y, MUTED))
	self.spawnLabel = track(UI.text("", 13, px + 12, y + 16, TEXT))
	y = y + 44
	track(UI.text("CEU", 11, px + 12, y, MUTED))
	self.skySwatches = {}
	for index, rgb in ipairs({
		{ 150, 200, 240 },
		{ 120, 170, 230 },
		{ 245, 190, 130 },
		{ 40, 45, 70 },
		{ 190, 220, 245 },
	}) do
		local x = px + 12 + (index - 1) * 44
		local swatch = track(UI.rect(x, y + 16, 38, 24, UI.color(rgb), 0))
		self.skySwatches[index] = { swatch = swatch, rgb = rgb }
	end
	y = y + 56
	track(UI.text("tecla P: spawn no bloco apontado", 11, px + 12, y, MUTED))
	y = y + 30
	self:button("setSpawn", "SPAWN AQUI", px + 12, y, 220, 28, Color3.new(0.18, 0.26, 0.36))
	y = y + 40
	track(UI.text("ARQUIVO", 11, px + 12, y, MUTED))
	self.fileLabel = track(UI.text("", 12, px + 12, y + 16, TEXT))
end

function UI:buildScriptsTab()
	local px = self.width - 264
	self.scriptsGroup = {}
	local function track(primitive)
		self.scriptsGroup[#self.scriptsGroup + 1] = primitive
		return self:track(primitive)
	end
	track(UI.text("SCRIPTS DO MAPA", 11, px + 12, 108, MUTED))
	track(UI.text("E: editar   Tab: alternar", 11, px + 12, 124, MUTED))
	self.scriptLines = {}
	for i = 1, 6 do
		local y = 146 + (i - 1) * 24
		local background = track(UI.rect(px + 10, y - 2, 228, 20, PANEL_SOFT, 0.5))
		local label = track(UI.text("", 12, px + 16, y + 1, TEXT))
		self.scriptLines[i] = { background = background, label = label }
	end
	self:button("scriptNew", "NOVO SCRIPT", px + 12, 300, 220, 26, Color3.new(0.16, 0.30, 0.42))
	self:button("scriptEdit", "EDITAR SELECT", px + 12, 332, 220, 26, PANEL_SOFT)
	self:button("scriptRun", "RODAR NO TESTE", px + 12, 364, 220, 26, Color3.new(0.14, 0.38, 0.22))
	track(UI.text("os scripts rodam ao clicar TESTAR", 11, px + 12, 396, MUTED))
end

function UI:buildBottomBar()
	local by = self.height - 40
	self:track(UI.rect(0, by, self.width, 40, PANEL, 0.02))
	self:track(UI.text(
		"LMB: colocar/selecionar   Shift+LMB: apagar   Alt+LMB: pintar   RMB: orbitar   MMB: mover   roda: zoom   Ctrl+Z/Y: undo   Ctrl+S: salvar   F5: testar",
		12,
		16,
		by + 13,
		MUTED
	))
end

function UI:buildReticle()
	self.reticleH = self:track(UI.rect(self.width * 0.5 - 7, self.height * 0.5 - 1, 14, 2, TEXT, 0.55))
	self.reticleV = self:track(UI.rect(self.width * 0.5 - 1, self.height * 0.5 - 7, 2, 14, TEXT, 0.55))
end

-- ---------------------------------------------------------------------------
-- Handlers
-- ---------------------------------------------------------------------------

function UI:onButton(id, fn)
	self.buttonHandlers[id] = fn
end

function UI:onPreset(index, fn)
	self.presetHandlers[index] = fn
end

function UI:onTab(id, fn)
	self.tabHandlers[id] = fn
end

function UI:onColor(fn)
	self.colorHandler = fn
end

function UI:onMaterial(fn)
	self.materialHandler = fn
end

function UI:onSky(fn)
	self.skyHandler = fn
end

function UI:onScript(fn)
	self.scriptHandler = fn
end

local function hit(rect, x, y)
	return rect ~= nil
		and x >= rect.x
		and x <= rect.x + rect.w
		and y >= rect.y
		and y <= rect.y + rect.h
end

--- Testa um clique. Devolve "viewport" quando nada da UI foi atingido.
function UI:click(x, y)
	for id, rect in pairs(self.buttons) do
		if hit(rect, x, y) then
			local handler = self.buttonHandlers[id]
			if handler then
				handler()
			end
			return "button:" .. id
		end
	end
	for id, rect in pairs(self.tabs) do
		if hit(rect, x, y) then
			self:setTab(id)
			return "tab:" .. id
		end
	end
	if x >= 16 and x <= 266 and y >= 58 then
		for index, rect in ipairs(self.presets) do
			if hit(rect, x, y) then
				local handler = self.presetHandlers[index]
				if handler then
					handler()
				end
				return "preset:" .. index
			end
		end
		return "palette"
	end
	local px = self.width - 264
	if x >= px then
		if self.tab == "inspector" then
			for index, swatch in ipairs(self.swatches) do
				local p = swatch.Position
				if x >= p.X and x <= p.X + 56 and y >= p.Y and y <= p.Y + 28 then
					self:selectColor(index)
					if self.colorHandler then
						self.colorHandler(index)
					end
					return "color:" .. index
				end
			end
			for index, label in ipairs(self.materialLabels) do
				local p = label.Position
				if x >= p.X - 4 and x <= p.X + 72 and y >= p.Y - 2 and y <= p.Y + 18 then
					self:selectMaterial(index)
					if self.materialHandler then
						self.materialHandler(index)
					end
					return "material:" .. index
				end
			end
		elseif self.tab == "map" then
			for index, entry in ipairs(self.skySwatches) do
				local p = entry.swatch.Position
				if x >= p.X and x <= p.X + 38 and y >= p.Y and y <= p.Y + 24 then
					if self.skyHandler then
						self.skyHandler(entry.rgb)
					end
					return "sky:" .. index
				end
			end
		elseif self.tab == "scripts" then
			for index, line in ipairs(self.scriptLines) do
				local p = line.label.Position
				if y >= p.Y - 4 and y <= p.Y + 18 then
					self.selectedScriptIndex = index
					if self.scriptHandler then
						self.scriptHandler(index)
					end
					return "script:" .. index
				end
			end
		end
	end
	return "viewport"
end

function UI:setTab(id)
	self.tab = id
	local function show(group, on)
		for _, primitive in ipairs(group or {}) do
			pcall(function()
				primitive.Visible = on
			end)
		end
	end
	show(self.inspectorGroup, id == "inspector")
	show(self.scriptsGroup, id == "scripts")
	show(self.mapGroup, id == "map")
	for tabId, rect in pairs(self.tabs) do
		rect.label.Color = (tabId == id) and ACCENT or MUTED
		rect.background.Transparency = (tabId == id) and 0.0 or 0.2
	end
end

-- ---------------------------------------------------------------------------
-- Atualizações
-- ---------------------------------------------------------------------------

function UI:setStatus(content, color)
	self.status.Text = content or ""
	self.status.Color = color or MUTED
end

function UI:selectColor(index)
	self.colorIndex = index
	for i, swatch in ipairs(self.swatches) do
		local selected = i == index
		local p = swatch.Position
		pcall(function()
			swatch.Size = dim(selected and 56 or 52, selected and 28 or 24)
			swatch.Position = dim(p.X - (selected and 2 or 0), p.Y - (selected and 2 or 0))
		end)
	end
end

function UI:selectMaterial(index)
	self.materialIndex = index
	for i, label in ipairs(self.materialLabels) do
		label.Color = i == index and ACCENT or MUTED
	end
end

function UI:selectPreset(name)
	for _, rect in ipairs(self.presets) do
		rect.swatch.Transparency = (rect.entry.name == name) and 0.0 or 0.05
	end
end

function UI:setSize(size)
	self.sizeLabel.Text = string.format("%d x %d x %d", size[1], size[2], size[3])
end

function UI:setSelection(text)
	self.selectionLabel.Text = text or "nenhum bloco"
end

function UI:setMapInfo(world, count, spawn, file)
	self.mapNameLabel.Text = tostring(world.name or "?")
	self.mapIdLabel.Text = tostring(world.id or "?")
	self.spawnLabel.Text = string.format("%.1f, %.1f, %.1f", spawn[1], spawn[2], spawn[3])
	self.fileLabel.Text = tostring(file or "nao salvo") .. "  (" .. count .. " blocos)"
end

function UI:setScripts(scripts, selected)
	for i, line in ipairs(self.scriptLines) do
		local file = scripts and scripts.files[i]
		line.label.Text = file and ("* " .. file.name) or ""
		local active = file and selected == file.name
		line.label.Color = active and ACCENT or TEXT
		line.background.Transparency = active and 0.1 or 0.5
	end
end

function UI:setInfo(text)
	self.presetFilterLabel.Text = text or ""
end

function UI:setVisible(on)
	self.visible = on
	for _, primitive in ipairs(self.primitives) do
		pcall(function()
			primitive.Visible = on
		end)
	end
	if on then
		self:setTab(self.tab)
	end
end

function UI:Destroy()
	for _, primitive in ipairs(self.primitives) do
		pcall(function()
			primitive:Destroy()
		end)
	end
	self.primitives = {}
end

UI.OK = OK
UI.WARN = WARN
UI.MUTED = MUTED
UI.TEXT = TEXT
UI.ACCENT = ACCENT
UI.PANEL = PANEL
UI.PANEL_SOFT = PANEL_SOFT
UI.DANGER = DANGER

return UI
