--[[
	HUD do cliente: lista de jogadores online (canto superior esquerdo),
	chat (canto inferior esquerdo) e linha de diagnóstico.

	Fundo: `GUI.Basic.Square`. Texto: `GUI.Basic.Font` com o `FontAsset` de
	`assets/fonts/DejaVuSans.ttf` (a engine não traz fonte embutida).
]]

local Hud = {}
Hud.__index = Hud

local GUI = import("GUI")
local Primitives = import("Primitives")
local Asset = import("Asset")

local Dim = Primitives.Dim.new
local Color3 = Primitives.Color3

local MAX_LINES = 7
local LINE_HEIGHT = 20
local FONT_PATH = "fonts/DejaVuSans.ttf"
local fontCache = nil

--- Fonte do HUD (carregada uma vez).
local function hudFont()
	if not fontCache then
		fontCache = Asset.GetAsset("Font", FONT_PATH)
	end
	return fontCache
end

local function box(width, height, x, y, color)
	local square = GUI.Basic.Square()
	square.Size = Dim(width, height)
	square.Position = Dim(x, y)
	square.Color = color or Color3.new(0.05, 0.05, 0.08)
	square.Transparency = 0.25
	return square
end

local function label(text, size, x, y, color)
	local primitive = GUI.Basic.Font(hudFont())
	primitive.Text = text
	primitive.TextSize = size or 16
	primitive.Position = Dim(x, y)
	primitive.Size = Dim(0, (size or 16) + 4)
	if color then
		primitive.Color = color
	end
	return primitive
end

function Hud.new()
	local self = setmetatable({}, Hud)
	self.lines = {}
	self.primitives = {}

	-- Painel de jogadores.
	self.playersPanel = box(220, 200, 16, 16)
	local title = label("LOOMIA", 18, 26, 24, Color3.new(0.45, 0.85, 1.0))
	local subtitle = label("carregando…", 13, 26, 48, Color3.new(0.72, 0.75, 0.8))
	self.primitives = { self.playersPanel, title, subtitle }
	self.playersSubtitle = subtitle
	self.playerLines = {}
	for i = 1, 8 do
		local line = label("", 14, 26, 66 + (i - 1) * 18, Color3.new(1, 1, 1))
		self.playerLines[i] = line
		self.primitives[#self.primitives + 1] = line
	end

	-- Painel de chat.
	self.chatPanel = box(560, MAX_LINES * LINE_HEIGHT + 16, 16, 520)
	table.insert(self.primitives, self.chatPanel)
	self.chatLines = {}
	for i = 1, MAX_LINES do
		local line = label("", 14, 26, 528 + (i - 1) * LINE_HEIGHT, Color3.new(1, 1, 1))
		line.Size = Dim(0, LINE_HEIGHT)
		self.chatLines[i] = line
		self.primitives[#self.primitives + 1] = line
	end

	-- Caixa de digitação: aparece só enquanto o chat está aberto.
	local inputY = 520 + MAX_LINES * LINE_HEIGHT + 6
	self.inputPanel = box(560, 30, 16, inputY, Color3.new(0.10, 0.11, 0.15))
	self.inputPanel.Transparency = 0.05
	self.inputPrefix = label("Dizendo:", 14, 26, inputY + 7, Color3.new(0.55, 0.85, 1.0))
	self.inputText = label("", 14, 96, inputY + 7, Color3.new(1, 1, 1))
	self.inputHint = label("", 12, 26, inputY + 36, Color3.new(0.55, 0.6, 0.68))
	self.primitives[#self.primitives + 1] = self.inputPanel
	self.primitives[#self.primitives + 1] = self.inputPrefix
	self.primitives[#self.primitives + 1] = self.inputText
	self.primitives[#self.primitives + 1] = self.inputHint
	self.inputPanel.Visible = false
	self.inputPrefix.Visible = false
	self.inputText.Visible = false
	self.inputHint.Visible = false
	self.hintTimer = 0

	-- Diagnóstico (canto superior direito).
	self.stats = label("", 13, 900, 16, Color3.new(0.72, 0.75, 0.8))
	table.insert(self.primitives, self.stats)
	self.lastStats = ""
	return self
end

function Hud:setPlayer(name)
	local list = { name .. " (você)" }
	self.playersSubtitle.Text = "1 online"
	self.playerLines[1].Text = list[1]
	for i = 2, #self.playerLines do
		self.playerLines[i].Text = ""
	end
	self.ownName = name
	self.players = { list[1] }
end

function Hud:updatePlayers(states)
	self.players = {}
	for _, state in ipairs(states) do
		local look = state.appearance or {}
		local name = look.username or look.name or "Jogador"
		if state.id == self.localId then
			name = name .. " (você)"
		end
		self.players[#self.players + 1] = name
	end
	table.sort(self.players)
	self.playersSubtitle.Text = string.format("%d online", #self.players)
	for i = 1, #self.playerLines do
		self.playerLines[i].Text = self.players[i] or ""
	end
end

function Hud:addChatLine(from, text, color)
	local line = label("", 14, 26, 0, color or Color3.new(1, 1, 1))
	line.Text = ("[%s] %s"):format(from, text)
	line.Size = Dim(0, LINE_HEIGHT)
	self.lines[#self.lines + 1] = line
	while #self.lines > MAX_LINES do
		local old = table.remove(self.lines, 1)
		pcall(function()
			old:Destroy()
		end)
	end
	self:layoutChat()
end

--- Reposiciona as linhas de chat (mais recentes embaixo, como no Bevy).
function Hud:layoutChat()
	local count = #self.lines
	for index, line in ipairs(self.lines) do
		local row = MAX_LINES - count + index
		line.Position = Dim(26, 528 + (row - 1) * LINE_HEIGHT)
		line.Visible = true
	end
end

function Hud:setStats(text)
	if text ~= self.lastStats then
		self.lastStats = text
		self.stats.Text = text
	end
end

-- Mostra/esconde a caixa "Dizendo:" do chat.
-- `open` controla a caixa; `hint` é a dica de atalho (some após 3 s).
function Hud:setChatInput(open, text, hint)
	local visible = open == true
	self.inputPanel.Visible = visible
	self.inputPrefix.Visible = visible
	self.inputText.Visible = visible

	if visible then
		local cursor = (math.floor(os.time() * 2) % 2 == 0) and "|" or " "
		self.inputText.Text = (text or "") .. cursor
	end

	if hint and hint ~= "" then
		self.inputHint.Text = hint
		self.hintTimer = 3.0
	end
	self.inputHint.Visible = (self.hintTimer or 0) > 0
end

-- Conta o tempo da dica do chat (chamado 1x por frame).
function Hud:update(dt)
	if (self.hintTimer or 0) > 0 then
		self.hintTimer = self.hintTimer - dt
		if self.hintTimer <= 0 then
			self.inputHint.Visible = false
		end
	end
end

function Hud:Destroy()
	for _, primitive in ipairs(self.primitives) do
		pcall(function()
			primitive:Destroy()
		end)
	end
	for _, line in ipairs(self.lines) do
		pcall(function()
			line:Destroy()
		end)
	end
end

return Hud
