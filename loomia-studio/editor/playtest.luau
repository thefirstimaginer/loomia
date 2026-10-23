--[[
	Runtime do Playtest.

	O Studio e o jogo vivem no MESMO processo/VM Luau (a engine só permite um
	`Window.Open`). O Playtest então:

	1. Esconde a UI e a cena do editor (Render = false).
	2. Reconstrói o mapa do documento numa cena própria.
	3. Cria o boneco no spawn e roda a física com o teclado do jogo.
	4. Executa os scripts do mapa (`editor/scripts.luau`) em ambientes
	   separados, cada um com `import` trocado: `import("Players")` devolve o
	   jogador do playtest. `print` e erros aparecem no overlay.
	5. Ao sair, restaura tudo como estava (o documento NUNCA é alterado).

	Atalhos durante o teste: Esc sai, F1 respawn, RMB orbita, LMB alterna 1ª pessoa.
]]

local Playtest = {}
Playtest.__index = Playtest

local Renderable = import("Renderable")
local Primitives = import("Primitives")
local Mouse = import("Mouse")
local GUI = import("GUI")
local GPU = import("GPU")
local Asset = import("Asset")


local Physics = require("../playtest/physics")
local Avatar = require("../playtest/avatar")
local Sky = require("./sky")

local Vector = Primitives.Vector
local CFrame = Primitives.CFrame
local Color3 = Primitives.Color3
local Dim = Primitives.Dim.new

local TICK = 1 / 30
local EYE_HEIGHT = 1.55

local PANEL = Color3.new(0.05, 0.06, 0.09)
local WHITE = Color3.new(1, 1, 1)
local MUTED = Color3.new(0.70, 0.74, 0.80)
local GREEN = Color3.new(0.45, 0.90, 0.60)

-- ---------------------------------------------------------------------------
-- API exposta aos scripts do mapa
-- ---------------------------------------------------------------------------

--- `import("Players").LocalPlayer` — superfície estável para os scripts.
local function makePlayerView(playtest)
	local api = {
		Position = function()
			local p = playtest.state.position
			return { X = p[1], Y = p[2], Z = p[3] }
		end,
		Velocity = function()
			local v = playtest.state.velocity
			return { X = v[1], Y = v[2], Z = v[3] }
		end,
		OnGround = function()
			return playtest.state.onGround
		end,
		Time = function()
			return playtest.elapsed
		end,
		Teleport = function(x, y, z)
			playtest.state.position = { x, y, z }
			playtest.state.velocity = { 0, 0, 0 }
			return true
		end,
		SetPosition = function(p)
			if type(p) == "table" then
				playtest.state.position = { p.X or p[1] or 0, p.Y or p[2] or 0, p.Z or p[3] or 0 }
				playtest.state.velocity = { 0, 0, 0 }
			end
			return true
		end,
		Say = function(text)
			playtest:log(tostring(text))
			return true
		end,
	}
	return setmetatable({}, {
		__index = function(_, key)
			return api[key]
		end,
	})
end

function Playtest:localPlayerView()
	if not self.playerView then
		self.playerView = makePlayerView(self)
	end
	return self.playerView
end

-- ---------------------------------------------------------------------------
-- Scripts do mapa
-- ---------------------------------------------------------------------------

--- Executa um script do mapa num ambiente próprio.
function Playtest:runScript(file)
	local view = self:localPlayerView()
	local env = {}
	setmetatable(env, { __index = _G })
	env.import = function(name)
		if name == "Players" then
			return { LocalPlayer = view, Players = { view }, Time = self.elapsed }
		end
		return import(name)
	end
	env.print = function(...)
		local parts = {}
		for i = 1, select("#", ...) do
			parts[i] = tostring(select(i, ...))
		end
		self:log(table.concat(parts, " "))
	end
	env.Map = {
		Name = self.map.name,
		Id = self.map.id,
		Blocks = self.map.blocks,
		Player = view,
	}
	env.GetPlayer = function()
		return view
	end

	local fn, compileErr = loadstring(file.source, "@" .. file.name)
	if not fn then
		self:log("erro ao compilar '" .. file.name .. "': " .. tostring(compileErr))
		return false
	end
	local ok, err = pcall(fn)
	if not ok then
		self:log("erro em '" .. file.name .. "': " .. tostring(err))
		return false
	end
	return true
end

function Playtest:runScripts()
	if not self.scripts then
		return
	end
	local ran = 0
	for _, file in ipairs(self.scripts.files) do
		if self:runScript(file) then
			ran = ran + 1
		end
	end
	if ran > 0 then
		self:log(string.format("%d script(s) do mapa carregado(s)", ran))
	end
end

function Playtest:log(text)
	print("[playtest] " .. text)
	self.messages[#self.messages + 1] = text
	while #self.messages > 5 do
		table.remove(self.messages, 1)
	end
	self:refreshOverlay()
end

-- ---------------------------------------------------------------------------
-- Overlay
-- ---------------------------------------------------------------------------

function Playtest:buildOverlay()
	self.overlay = {}
	local function track(primitive)
		self.overlay[#self.overlay + 1] = primitive
		return primitive
	end
	local function label(text, size, x, y, color)
		local primitive = GUI.Basic.Font(self.font)
		primitive.Text = text
		primitive.TextSize = size or 14
		primitive.Position = Dim(x, y)
		primitive.Size = Dim(0, (size or 14) + 4)
		primitive.Color = color or WHITE
		return primitive
	end

	local box = GUI.Basic.Square()
	box.Size = Dim(560, 30)
	box.Position = Dim(16, 16)
	box.Color = PANEL
	box.Transparency = 0.25
	track(box)

	self.statusLabel = track(label("PLAYTEST", 15, 26, 21, GREEN))
	self.hintLabel = track(label("Esc: sair   F1: respawn   RMB: camera   LMB: 1a pessoa", 12, 250, 23, MUTED))
	self.logLabel = track(label("", 13, 16, self.height - 34, WHITE))
end

function Playtest:refreshOverlay()
	if not self.logLabel then
		return
	end
	local lines = {}
	for i = math.max(1, #self.messages - 2), #self.messages do
		lines[#lines + 1] = self.messages[i]
	end
	self.logLabel.Text = table.concat(lines, "   |   ")
end

-- ---------------------------------------------------------------------------
-- Ciclo de vida
-- ---------------------------------------------------------------------------

--- @param doc Document, scripts Scripts, opts { scene, ui, camera, font, skyShader, width, height }
function Playtest.new(doc, scripts, opts)
	local self = setmetatable({}, Playtest)
	self.doc = doc
	self.scripts = scripts
	self.world = doc.world
	self.map = doc.world
	self.opts = opts or {}
	self.font = self.opts.font or Asset.GetAsset("Font", "fonts/DejaVuSans.ttf")
	self.width = self.opts.width or 1280
	self.height = self.opts.height or 720
	self.elapsed = 0
	self.messages = {}
	self.parts = {}
	self.avatar = nil
	self.running = false
	self.requestStop = false
	self.state = {
		position = { 0, 0, 0 },
		velocity = { 0, 0, 0 },
		yaw = 0,
		onGround = false,
	}
	self.keys = {}
	self.orbit = { yaw = math.pi * 0.75, pitch = -0.35, distance = 14 }
	self.firstPerson = false
	self.cameraDragging = false
	self.accumulator = 0
	return self
end

function Playtest:camera()
	local p = self.state.position
	local eye = Vector.new(p[1], p[2] + EYE_HEIGHT, p[3])
	local yaw, pitch, distance = self.orbit.yaw, self.orbit.pitch, self.orbit.distance
	local offset = Vector.new(
		-math.sin(yaw) * math.cos(pitch) * distance,
		math.sin(pitch) * distance,
		-math.cos(yaw) * math.cos(pitch) * distance
	)
	if self.firstPerson then
		offset = Vector.new(0, 0, 0)
	end
	return CFrame.LookAt(eye + offset, eye, Vector.new(0, 1, 0))
end

function Playtest:start()
	self.running = true
	self.requestStop = false
	self.elapsed = 0
	self.camera = self.opts.camera
	self.previousCamera = self.camera.CFrame

	if self.opts.scene then
		self.opts.scene:setVisible(false)
	end
	if self.opts.ui then
		self.opts.ui:setVisible(false)
	end

	self:buildWorld()

	local spawn = self.world.spawn or { 0, 0.2, 0 }
	self.state.position = { spawn[1], spawn[2], spawn[3] }
	self.state.velocity = { 0, 0, 0 }
	self.state.onGround = false
	self.state.yaw = 0
	self.avatar = Avatar.new(self.world.preview or {})
	self:applySky()
	self.camera.CFrame = self:camera()

	self:buildOverlay()
	self:log("mundo '" .. tostring(self.world.name or self.world.id) .. "' iniciado")
	self:runScripts()
	Mouse.Unlock()
	return true
end

function Playtest:applySky()
	Sky.apply(self.world)
end

function Playtest:buildWorld()
	for _, part in ipairs(self.parts) do
		pcall(function()
			part:Destroy()
		end)
	end
	self.parts = {}
	self.minY, self.maxY = math.huge, -math.huge

	for _, block in ipairs(self.world.blocks or {}) do
		local rgb = block.color or { 160, 160, 160 }
		local part = Renderable.BasePart("Cube")
		part.Size = Vector.new(block.size[1], block.size[2], block.size[3])
		part.CFrame = CFrame.new(
			Vector.new(block.position[1], block.position[2], block.position[3]),
			Vector.new(0, 0, 0)
		)
		part.Color = Color3.new(rgb[1] / 255, rgb[2] / 255, rgb[3] / 255)
		part.CastShadow = true
		part.ReceiveShadow = true
		part.Lit = block.material ~= "neon"
		self.parts[#self.parts + 1] = part
		local half = block.size[2] * 0.5
		self.minY = math.min(self.minY, block.position[2] - half)
		self.maxY = math.max(self.maxY, block.position[2] + half)
	end
	if self.minY == math.huge then
		self.minY, self.maxY = 0, 0
	end
end

function Playtest:stop()
	self.running = false
	for _, part in ipairs(self.parts) do
		pcall(function()
			part:Destroy()
		end)
	end
	self.parts = {}
	if self.avatar then
		self.avatar:Destroy()
		self.avatar = nil
	end
	for _, primitive in ipairs(self.overlay or {}) do
		pcall(function()
			primitive:Destroy()
		end)
	end
	self.overlay = {}
	self.statusLabel = nil
	self.hintLabel = nil
	self.logLabel = nil

	if self.opts.scene then
		self.opts.scene:setVisible(true)
	end
	if self.opts.ui then
		self.opts.ui:setVisible(true)
	end
	if self.camera and self.previousCamera then
		self.camera.CFrame = self.previousCamera
	end
	Mouse.Unlock()
end

-- ---------------------------------------------------------------------------
-- Input (do jogo, não do editor)
-- ---------------------------------------------------------------------------

function Playtest:onKey(key, down)
	if not self.running then
		return
	end
	if down and key == "Escape" then
		self.requestStop = true
		return
	end
	if down and key == "F1" then
		local spawn = self.world.spawn or { 0, 0.2, 0 }
		self.state.position = { spawn[1], spawn[2], spawn[3] }
		self.state.velocity = { 0, 0, 0 }
		return
	end
	self.keys[key] = down
end

function Playtest:onMouseButton(button, down)
	if not self.running then
		return
	end
	if button == "MouseButton2" then
		self.cameraDragging = down
		if down then
			Mouse.Lock()
		else
			Mouse.Unlock()
		end
	elseif button == "MouseButton1" and down then
		self.firstPerson = not self.firstPerson
	end
end

function Playtest:onMouseMove(delta)
	if not self.running then
		return
	end
	if not self.cameraDragging and not self.firstPerson then
		return
	end
	self.orbit.yaw = self.orbit.yaw - delta.X * 0.005
	self.orbit.pitch = math.max(-1.25, math.min(0.6, self.orbit.pitch - delta.Y * 0.005))
end

function Playtest:onScroll(dy)
	if not self.running then
		return
	end
	self.orbit.distance = math.max(3, math.min(24, self.orbit.distance - dy * 1.1))
end

-- ---------------------------------------------------------------------------
-- Frame
-- ---------------------------------------------------------------------------

local function readMove(self)
	local forward, right = 0, 0
	if self.keys.W or self.keys.ArrowUp then
		forward = forward + 1
	end
	if self.keys.S or self.keys.ArrowDown then
		forward = forward - 1
	end
	if self.keys.D or self.keys.ArrowRight then
		right = right + 1
	end
	if self.keys.A or self.keys.ArrowLeft then
		right = right - 1
	end
	local yaw = self.orbit.yaw
	local sin, cos = math.sin(yaw), math.cos(yaw)
	return right * cos + forward * sin, right * -sin + forward * cos
end

function Playtest:update(dt)
	if not self.running then
		return
	end
	dt = math.min(dt, 0.1)
	self.elapsed = self.elapsed + dt

	self.accumulator = self.accumulator + dt
	local steps = 0
	while self.accumulator >= TICK and steps < 4 do
		self.accumulator = self.accumulator - TICK
		steps = steps + 1
		local mx, mz = readMove(self)
		local input = { moveX = mx, moveZ = mz, jump = self.keys.Space == true }
		self.state = Physics.step(self.state, input, self.world.blocks or {}, self.minY, self.maxY, TICK)
		if self.firstPerson then
			self.state.yaw = self.orbit.yaw
		end
	end

	if self.state.position[2] < self.minY - 30 then
		local spawn = self.world.spawn or { 0, 0.2, 0 }
		self.state.position = { spawn[1], spawn[2], spawn[3] }
		self.state.velocity = { 0, 0, 0 }
	end

	if self.avatar then
		self.avatar:setPosition(self.state.position[1], self.state.position[2], self.state.position[3])
		self.avatar.yaw = self.state.yaw
		local v = self.state.velocity
		self.avatar:apply(dt, math.sqrt(v[1] * v[1] + v[3] * v[3]))
	end
	if self.camera then
		self.camera.CFrame = self:camera()
	end

	if self.statusLabel then
		local fps = GPU.FrameStats().FPS or 0
		self.statusLabel.Text = string.format(
			"PLAYTEST   FPS %d   pos %.1f, %.1f, %.1f",
			math.floor(fps),
			self.state.position[1],
			self.state.position[2],
			self.state.position[3]
		)
	end
end

return Playtest
