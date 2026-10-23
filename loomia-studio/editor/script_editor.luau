--[[
	Janela de edição de scripts do mapa (sem janela separada: a engine só
	permite UM `Window.Open`, então o editor de código é um painel dentro da
	mesma janela).

	Modos:
		normal    navegação (fechar, trocar de arquivo, novo)
		editing   digitação no editor (texto, Enter, Backspace, setas)

	`Ctrl+Enter` compila o script imediatamente e mostra erros de sintaxe.
	Texturas de fonte: a engine não tem "edit box", então o cursor é um
	retângulo e o texto é redesenhado a cada tecla (limitado a 30 linhas
	visíveis para manter barato).
]]

local ScriptEditor = {}
ScriptEditor.__index = ScriptEditor

local GUI = import("GUI")
local Keyboard = import("Keyboard")
local Primitives = import("Primitives")
local Asset = import("Asset")

local Dim = Primitives.Dim.new
local Color3 = Primitives.Color3

local PANEL = Color3.new(0.05, 0.055, 0.075)
local BAR = Color3.new(0.09, 0.10, 0.14)
local TEXT = Color3.new(0.90, 0.93, 0.96)
local MUTED = Color3.new(0.58, 0.62, 0.70)
local ACCENT = Color3.new(0.42, 0.84, 1.0)
local DANGER = Color3.new(0.95, 0.42, 0.38)
local OK = Color3.new(0.55, 0.9, 0.6)

local MAX_VISIBLE_LINES = 30
local LINE_HEIGHT = 18

local fontCache = nil
local function font()
	if not fontCache then
		fontCache = Asset.GetAsset("Font", "fonts/DejaVuSansMono.ttf")
	end
	return fontCache
end

local function mono(content, size, x, y, color)
	local primitive = GUI.Basic.Font(font())
	primitive.Text = content
	primitive.TextSize = size or 14
	primitive.Position = Dim(x, y)
	primitive.Size = Dim(0, (size or 14) + 4)
	primitive.Color = color or TEXT
	return primitive
end

function ScriptEditor.new(scripts, opts)
	local self = setmetatable({}, ScriptEditor)
	self.scripts = scripts
	self.opts = opts or {}
	self.width = self.opts.width or 1280
	self.height = self.opts.height or 720
	self.open = false
	self.mode = "normal"
	self.active = scripts.active
	self.lines = {}
	self.row = 1
	self.col = 1
	self.scroll = 0
	self.primitives = {}
	self.lineLabels = {}
	self.status = ""
	self.statusColor = MUTED
	return self
end

function ScriptEditor:track(primitive)
	self.primitives[#self.primitives + 1] = primitive
	return primitive
end

--- Divide o source em linhas (preserva linhas vazias).
local function splitLines(source)
	local lines = {}
	local current = {}
	for i = 1, #source do
		local ch = source:sub(i, i)
		if ch == "\n" then
			lines[#lines + 1] = table.concat(current)
			current = {}
		else
			current[#current + 1] = ch
		end
	end
	lines[#lines + 1] = table.concat(current)
	return lines
end

function ScriptEditor:load(name)
	self.active = name or self.active
	self.source = self.scripts:get(self.active)
	self.lines = splitLines(self.source)
	self.row = 1
	self.col = 1
	self.scroll = 0
end

function ScriptEditor:currentLine()
	return self.lines[self.row] or ""
end

function ScriptEditor:setSourceFromLines()
	self.source = table.concat(self.lines, "\n")
	self.scripts:set(self.active, self.source)
end

-- ---------------------------------------------------------------------------
-- Layout
-- ---------------------------------------------------------------------------

function ScriptEditor:build()
	local w, h = self.width, self.height
	local x, y = 80, 60
	local panelW, panelH = w - 160, h - 120
	self.panel = {
		x = x,
		y = y,
		w = panelW,
		h = panelH,
	}
	self:track(GUI.Basic.Square())
	local background = self.primitives[#self.primitives]
	background.Size = Dim(panelW, panelH)
	background.Position = Dim(x, y)
	background.Color = PANEL
	background.Transparency = 0.02

	-- Barra superior do editor.
	self:track(GUI.Basic.Square())
	local bar = self.primitives[#self.primitives]
	bar.Size = Dim(panelW, 34)
	bar.Position = Dim(x, y)
	bar.Color = BAR
	bar.Transparency = 0.0

	self.titleLabel = self:track(mono("", 15, x + 12, y + 9, ACCENT))
	self.fileLabel = self:track(mono("", 12, x + 260, y + 11, MUTED))
	self.statusLabel = self:track(mono("", 12, x + panelW - 320, y + 11, MUTED))

	-- Lista de arquivos (à esquerda do código).
	self.tabs = {}
	for i = 1, math.min(8, #self.scripts.files) do
		local entry = self.scripts.files[i]
		local swatch = self:track(GUI.Basic.Square())
		swatch.Size = Dim(150, 22)
		swatch.Position = Dim(x + 10, y + 44 + (i - 1) * 24)
		swatch.Color = BAR
		swatch.Transparency = 0.35
		local label = self:track(mono(entry.name, 12, x + 16, y + 46 + (i - 1) * 24, TEXT))
		self.tabs[i] = { name = entry.name, swatch = swatch, label = label }
	end

	-- Área de código.
	self.codeX = x + 176
	self.codeY = y + 44
	self.codeW = panelW - 186
	self.codeH = panelH - 88
	for i = 1, MAX_VISIBLE_LINES do
		local label = self:track(mono("", 14, self.codeX + 34, self.codeY + (i - 1) * LINE_HEIGHT, TEXT))
		self.lineLabels[i] = label
	end
	self:track(GUI.Basic.Square())
	self.gutter = self.primitives[#self.primitives]
	self.gutter.Size = Dim(30, self.codeH)
	self.gutter.Position = Dim(self.codeX, self.codeY)
	self.gutter.Color = Color3.new(0.03, 0.03, 0.05)
	self.gutter.Transparency = 0.0

	self.caret = self:track(GUI.Basic.Square())
	self.caret.Size = Dim(2, LINE_HEIGHT - 2)
	self.caret.Position = Dim(self.codeX + 34, self.codeY)
	self.caret.Color = ACCENT
	self.caret.Transparency = 0.0

	-- Rodapé: comandos.
	self:track(GUI.Basic.Square())
	local footer = self.primitives[#self.primitives]
	footer.Size = Dim(panelW, 26)
	footer.Position = Dim(x, y + panelH - 26)
	footer.Color = BAR
	footer.Transparency = 0.0
	self:track(mono(
		"TAB: volta ao editor    Ctrl+Enter: compilar    Ctrl+S: salvar    Enter: nova linha",
		12,
		x + 12,
		y + panelH - 20,
		MUTED
	))

	self:refresh()
	return self
end

function ScriptEditor:setVisible(on)
	self.open = on
	for _, primitive in ipairs(self.primitives) do
		pcall(function()
			primitive.Visible = on
		end)
	end
	if on then
		self:refresh()
	end
end

-- ---------------------------------------------------------------------------
-- Renderização do código
-- ---------------------------------------------------------------------------

function ScriptEditor:refresh()
	if not self.lineLabels then
		return
	end
	self.titleLabel.Text = "SCRIPTS DO MAPA — editando"
	self.fileLabel.Text = self.active or ""
	local total = #self.lines
	for i = 1, MAX_VISIBLE_LINES do
		local index = self.scroll + i
		local line = self.lines[index]
		if line then
			-- Número da linha + conteúdo (tabs viram 4 espaços).
			local shown = line:gsub("\t", "    ")
			self.lineLabels[i].Text = string.format("%4d  %s", index, shown)
			self.lineLabels[i].Visible = self.open
		else
			self.lineLabels[i].Text = ""
		end
	end
	-- Cursor.
	local visibleRow = self.row - self.scroll
	if visibleRow >= 1 and visibleRow <= MAX_VISIBLE_LINES then
		local col = math.min(self.col, #self:currentLine())
		self.caret.Position = Dim(self.codeX + 34 + col * 8, self.codeY + (visibleRow - 1) * LINE_HEIGHT)
		self.caret.Visible = self.open
	else
		self.caret.Visible = false
	end
	self:updateTabs()
	self:setStatus(self.status, self.statusColor)
end

function ScriptEditor:updateTabs()
	for _, tab in ipairs(self.tabs or {}) do
		local active = tab.name == self.active
		tab.label.Color = active and ACCENT or TEXT
		tab.swatch.Transparency = active and 0.0 or 0.35
	end
end

function ScriptEditor:setStatus(text, color)
	self.status = text or ""
	self.statusColor = color or MUTED
	if self.statusLabel then
		self.statusLabel.Text = self.status
		self.statusLabel.Color = self.statusColor
	end
end

function ScriptEditor:ensureVisible()
	if self.row - 1 < self.scroll then
		self.scroll = self.row - 1
	elseif self.row > self.scroll + MAX_VISIBLE_LINES then
		self.scroll = self.row - MAX_VISIBLE_LINES
	end
end

-- ---------------------------------------------------------------------------
-- Edição
-- ---------------------------------------------------------------------------

function ScriptEditor:insertText(text)
	local line = self:currentLine()
	local col = math.min(self.col, #line)
	self.lines[self.row] = line:sub(1, col) .. text .. line:sub(col + 1)
	self.col = col + #text
	self:setSourceFromLines()
end

function ScriptEditor:newline()
	local line = self:currentLine()
	local col = math.min(self.col, #line)
	local before, after = line:sub(1, col), line:sub(col + 1)
	-- Continua a indentação da linha atual.
	local indent = before:match("^[ \t]*") or ""
	self.lines[self.row] = before
	table.insert(self.lines, self.row + 1, indent .. after)
	self.row = self.row + 1
	self.col = #indent + 1
	self:setSourceFromLines()
end

function ScriptEditor:backspace()
	local line = self:currentLine()
	local col = math.min(self.col, #line)
	if col > 0 then
		self.lines[self.row] = line:sub(1, col - 1) .. line:sub(col + 1)
		self.col = col - 1
	elseif self.row > 1 then
		local previous = self.lines[self.row - 1]
		self.col = #previous + 1
		self.lines[self.row - 1] = previous .. line
		table.remove(self.lines, self.row)
		self.row = self.row - 1
	end
	self:setSourceFromLines()
end

function ScriptEditor:moveHorizontal(delta)
	self.col = math.max(1, self.col + delta)
end

function ScriptEditor:moveVertical(delta)
	self.row = math.max(1, math.min(#self.lines, self.row + delta))
	self.col = math.min(self.col, #self:currentLine() + 1)
	self:ensureVisible()
end

function ScriptEditor:compile()
	local fn, err = loadstring(self.source, "@" .. tostring(self.active))
	if fn then
		self:setStatus("sintaxe OK", OK)
	else
		self:setStatus("erro: " .. tostring(err):gsub("\n", " "), DANGER)
	end
end

-- ---------------------------------------------------------------------------
-- Input
-- ---------------------------------------------------------------------------

function ScriptEditor:onKey(key, down, ctrl)
	if not self.open or not down then
		return false
	end
	if key == "Escape" then
		self:setVisible(false)
		if self.opts.onClose then
			self.opts.onClose()
		end
		return true
	end
	if ctrl and key == "S" then
		if self.opts.onSave then
			self.opts.onSave()
		end
		return true
	end
	if ctrl and (key == "Return" or key == "Enter" or key == "KeypadEnter") then
		self:compile()
		self:refresh()
		return true
	end

	if key == "Return" or key == "Enter" or key == "KeypadEnter" then
		self:newline()
	elseif key == "Backspace" then
		self:backspace()
	elseif key == "Tab" then
		self:insertText("  ")
	elseif key == "Left" or key == "ArrowLeft" then
		self:moveHorizontal(-1)
	elseif key == "Right" or key == "ArrowRight" then
		self:moveHorizontal(1)
	elseif key == "Up" or key == "ArrowUp" then
		self:moveVertical(-1)
	elseif key == "Down" or key == "ArrowDown" then
		self:moveVertical(1)
	elseif key == "Space" then
		self:insertText(" ")
	elseif #key == 1 then
		-- A engine entrega letras maiúsculas; Shift decide a caixa.
		local typed = key
		if ctrl then
			return false
		end
		local shift = self.opts.isShift and self.opts.isShift() or false
		if not shift then
			typed = typed:lower()
		end
		self:insertText(typed)
	else
		-- Símbolos: a engine dá o nome da tecla física.
		local symbols = {
			Minus = "-",
			Equals = "=",
			LeftBracket = "[",
			RightBracket = "]",
			Backslash = "\\",
			Semicolon = ";",
			Quote = "'",
			Comma = ",",
			Period = ".",
			Slash = "/",
			Backquote = "`",
			One = "1",
			Two = "2",
			Three = "3",
			Four = "4",
			Five = "5",
			Six = "6",
			Seven = "7",
			Eight = "8",
			Nine = "9",
			Zero = "0",
		}
		local symbol = symbols[key]
		if symbol then
			self:insertText(symbol)
		else
			return false
		end
	end
	self:refresh()
	return true
end

--- Clique dentro do editor: alternar entre arquivos ou posicionar o cursor.
function ScriptEditor:onClick(x, y)
	if not self.open then
		return false
	end
	for index, tab in ipairs(self.tabs) do
		local swatch = tab.swatch
		local px, py = swatch.Position.X, swatch.Position.Y
		if x >= px and x <= px + 150 and y >= py and y <= py + 22 then
			if index <= #self.scripts.files then
				self:setVisible(false)
				self.active = self.scripts.files[index].name
				self:load(self.active)
				self:setVisible(true)
			end
			return true
		end
	end
	if x >= self.codeX and x <= self.codeX + self.codeW
		and y >= self.codeY and y <= self.codeY + self.codeH then
		local row = math.floor((y - self.codeY) / LINE_HEIGHT) + 1 + self.scroll
		self.row = math.max(1, math.min(#self.lines, row))
		self.col = math.max(1, math.floor((x - self.codeX - 34) / 8) + 1)
		self:refresh()
		return true
	end
	return false
end

function ScriptEditor:Destroy()
	for _, primitive in ipairs(self.primitives) do
		pcall(function()
			primitive:Destroy()
		end)
	end
	self.primitives = {}
end

return ScriptEditor
