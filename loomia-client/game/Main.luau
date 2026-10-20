--[[
	Loomia — cliente 3D (engine Ruzit + Luau).

	Fluxo:
	  1. Abre a janela e liga os sinais de input.
	  2. Conecta no WebSocket do servidor e espera o `welcome`.
	  3. O `welcome` traz o mapa (`.loom`), os jogadores e o spawn.
	  4. Reconstrói o mundo, cria os avatares e entra no loop:
	         input → previsão local (30 Hz) → envio
	         snapshots do servidor → reconciliação
	         câmera orbital + animação + HUD

	O mapa é autoritativo do servidor: o cliente só lê e renderiza.
]]

local Window = import("Window")
local Primitives = import("Primitives")
local Process = import("Process")
local Keyboard = import("Keyboard")
local Mouse = import("Mouse")
local GPU = import("GPU")
local Task = import("Task")

local Config = require("./config")
local Link = require("./shared/link")
local Map = require("./shared/map")
local Avatar = require("./shared/avatar")
local Physics = require("./shared/physics")
local Hud = require("./shared/hud")

local Vector = Primitives.Vector
local CFrame = Primitives.CFrame

local TICK_RATE = 30
local TICK_DT = 1 / TICK_RATE
local EYE_HEIGHT = 1.55

local win = Window.Open({
	title = "Loomia",
	width = 1280,
	height = 720,
})
local camera = win:GetViewport()
camera.FOV = 70
camera.Near = 0.1
camera.Far = 2000

GPU.SetMaxFrameRate(60)

-- ---------------------------------------------------------------------------
-- Estado
-- ---------------------------------------------------------------------------

local state = {
	world = nil,
	worldParts = nil,
	minY = 0,
	blocks = {},
	ready = false,
	playerId = nil,
	sequence = 0,
	accumulator = 0,
	sendAccumulator = 0,
	local_ = {
		position = { 0, 0, 0 },
		velocity = { 0, 0, 0 },
		yaw = 0,
		onGround = false,
	},
	orbit = { yaw = 0, pitch = -0.35, distance = 14 },
	avatars = {},      -- [player_id] = Avatar
	remote = {},       -- [player_id] = { position, yaw, velocity, appearance }
	previous = {},     -- [player_id] = posição anterior (para estimar velocidade)
	speeds = {},       -- [player_id] = velocidade horizontal suavizada
	firstPerson = false,
	chatOpen = false,
	chatBuffer = "",
	chatLog = {},      -- fila de mensagens digitadas (envio com Enter)
	pingTick = 0,
	startedAt = os.clock(),
}

local link = nil
local hud = nil

-- ---------------------------------------------------------------------------
-- Câmera orbital
-- ---------------------------------------------------------------------------

local function cameraCFrame()
    local eye = Vector.new(
        state.local_.position[1],
        state.local_.position[2] + EYE_HEIGHT,
        state.local_.position[3]
    )
    local yaw, pitch, distance = state.orbit.yaw, state.orbit.pitch, state.orbit.distance
    
    -- Adicionando os sinais de menos (-) para posicionar a câmera ATRÁS do avatar
    local offset = Vector.new(
        -math.sin(yaw) * math.cos(pitch) * distance,
        math.sin(pitch) * distance,
        -math.cos(yaw) * math.cos(pitch) * distance
    )
    
    if state.firstPerson then
        offset = Vector.new(0, 0, 0)
    end
    
    local target = eye + offset
    return CFrame.LookAt(target, eye, Vector.new(0, 1, 0))
end

local function updateCamera()
	camera.CFrame = cameraCFrame()
end

-- ---------------------------------------------------------------------------
-- Input
-- ---------------------------------------------------------------------------

--[[
	Sinais de input da engine (assinaturas reais, de `runtime/src/libs/input.rs`):
	  Mouse.Moved          (pos: Dim, delta: Dim)
	  Mouse.InputReceived  (botão: string, estado: "Begin" | "End")
	  Keyboard.InputChanged(id: number, nome: string, estado: "Begin" | "End")
	Os nomes seguem winit: "Space", "Return", "Escape" e letras maiúsculas ("W").
]]
local cameraDragging = false
state.keys = {}

local function bindInput()
	Mouse.InputReceived:Connect(function(button, state_)
		if button ~= "MouseButton1" and button ~= "MouseButton2" then
			return
		end
		cameraDragging = state_ == "Begin"
		if not state.firstPerson then
			if cameraDragging then
				Mouse.Lock()
			else
				Mouse.Unlock()
			end
		end
	end)

	Mouse.Moved:Connect(function(_pos, delta)
			if not state.firstPerson and not cameraDragging then
				return
			end
			-- Se o movimento horizontal do mouse estiver mexendo na inclinação, inverta ou ajuste os deltas:
			state.orbit.yaw = state.orbit.yaw - delta.X * 0.005
			state.orbit.pitch = math.max(-1.25, math.min(0.6, state.orbit.pitch - delta.Y * 0.005))
		end)

	Keyboard.InputChanged:Connect(function(_id, key, kind)
		local name = key
		-- Movimento contínuo: guarda o estado de cada tecla (down/up), não só
		-- o evento de pressionar — senão WASD "aperta e solta" a cada repeat.
		if name == "W" or name == "A" or name == "S" or name == "D" or name == "Space" then
			state.keys[name] = kind == "Begin"
			return
		end
		if kind ~= "Begin" then
			return
		end
		if name == "Escape" then
			state.chatOpen = false
			cameraDragging = false
			Mouse.Unlock()
		elseif state.chatOpen then
			if name == "Return" or name == "KeypadEnter" then
				local text = state.chatBuffer
				state.chatBuffer = ""
				state.chatOpen = false
				if text ~= "" then
					link:send({ type = "chat", text = text })
				end
			elseif name == "Backspace" then
				state.chatBuffer = state.chatBuffer:sub(1, #state.chatBuffer - 1)
			elseif #name == 1 and #state.chatBuffer < 140 then
				state.chatBuffer = state.chatBuffer .. name:lower()
			end
		elseif name == "T" then
			state.chatOpen = true
		elseif name == "R" then
			link:send({ type = "respawn" })
		elseif name == "V" then
			state.firstPerson = not state.firstPerson
			state.orbit.distance = state.firstPerson and 0.01 or 14
		end
	end)

	-- Zoom com a roda do mouse.
	Mouse.Scrolled:Connect(function(_dx, dy)
		if state.firstPerson then
			return
		end
		state.orbit.distance = math.max(3, math.min(24, state.orbit.distance - dy * 1.1))
	end)
end

local function readMoveAxes()
	local forward, right = 0, 0
	if state.keys.W or state.keys.ArrowUp then
		forward = forward + 1
	end
	if state.keys.S or state.keys.ArrowDown then
		forward = forward - 1
	end
	if state.keys.D or state.keys.ArrowRight then
		right = right + 1
	end
	if state.keys.A or state.keys.ArrowLeft then
		right = right - 1
	end
	-- A câmera fica atrás do personagem e olha para onde ele anda.
	-- Na Ruzit o forward é +Z: (sin yaw, 0, cos yaw); a direita é (cos yaw, 0, -sin yaw).
	local yaw = state.orbit.yaw
	local sin, cos = math.sin(yaw), math.cos(yaw)
	local x = right * cos + forward * sin
	local z = right * -sin + forward * cos
	return x, z
end

-- ---------------------------------------------------------------------------
-- Rede
-- ---------------------------------------------------------------------------

local function _playerState(id)
	local remote = state.remote[id]
	if remote then
		return remote
	end
	return state.local_
end

local function rebuildWorld(world)
	if state.worldParts then
		state.worldParts:Destroy()
	end
	state.world = world
	state.blocks = world.blocks or {}
	local parts = Map.new(world):build()
	state.worldParts = parts
	state.minY, state.maxY = parts.minY, parts.maxY
	Map.applySky(world)
	print(string.format(
		"[Loomia] mundo '%s': %d blocos → %d parts",
		world.name or world.id or "?",
		#state.blocks,
		parts.count
	))
end

local function onWelcome(message)
	-- O `Welcome` do servidor usa snake_case (`player_id`, `recent_chat`,
	-- `tick_rate`): o `InputState`/`PlayerStateWire` não têm `rename_all`.
	local playerId = tonumber(message.player_id) or tonumber(message.playerId)
	state.playerId = playerId
	state.ready = false
	rebuildWorld(message.map)

	local me = nil
	for _, player in ipairs(message.players or {}) do
		if player.id == playerId then
			me = player
		else
			state.remote[player.id] = {
				position = player.position,
				velocity = player.velocity or { 0, 0, 0 },
				yaw = player.yaw or 0,
				onGround = player.onGround or false,
				appearance = player.appearance,
			}
		end
	end

	local spawn = message.map.spawn or { 0, 0, 0 }
	local start = me and me.position or spawn
	state.local_ = {
		position = { start[1], start[2], start[3] },
		velocity = { 0, 0, 0 },
		yaw = me and me.yaw or 0,
		onGround = true,
	}
	state.ready = true
	state.tickRate = message.tick_rate or message.tickRate or TICK_RATE

	if hud then
		hud.localId = playerId
		hud:setPlayer(Config.playerName)
		hud:updatePlayers(message.players or {})
		for _, entry in ipairs(message.recent_chat or message.recentChat or {}) do
			hud:addChatLine(entry[1], entry[2])
		end
	end

	camera.CFrame = cameraCFrame()
	print(string.format("[Loomia] conectado como id %d ('%s')", playerId or -1, Config.playerName))
end

local function onSnapshot(message)
	for _, player in ipairs(message.players or {}) do
		if player.id == state.playerId then
			-- Reconciliação: corrige a previsão local com o estado autoritativo.
			local authoritative = { player.position[1], player.position[2], player.position[3] }
			local p = state.local_.position
			local dx, dz = p[1] - authoritative[1], p[3] - authoritative[3]
			local dy = p[2] - authoritative[2]
			local drift = math.sqrt(dx * dx + dz * dz)
			if (drift > 1.5 or math.abs(dy) > 1.5) and (state.sequence - (player.ack or 0)) < 15 then
				state.local_.position = authoritative
				state.local_.velocity = { player.velocity[1], player.velocity[2], player.velocity[3] }
				state.local_.onGround = player.onGround
			elseif drift > 0.2 then
				state.local_.position[1] = p[1] + (authoritative[1] - p[1]) * 0.12
				state.local_.position[2] = p[2] + (authoritative[2] - p[2]) * 0.12
				state.local_.position[3] = p[3] + (authoritative[3] - p[3]) * 0.12
			end
		else
			state.remote[player.id] = {
				position = player.position,
				velocity = player.velocity or { 0, 0, 0 },
				yaw = player.yaw or 0,
				onGround = player.onGround or false,
				appearance = player.appearance,
			}
		end
	end

	if hud and message.tick and message.tick % 30 == 0 then
		local list = {}
		for id, remote in pairs(state.remote) do
			list[#list + 1] = { id = id, appearance = remote.appearance }
		end
		for _, p in ipairs(message.players or {}) do
			if p.id == state.playerId then
				table.insert(list, 1, { id = p.id, appearance = p.appearance })
			end
		end
		hud:updatePlayers(list)
	end
end

local function onChat(message)
	if hud then
		hud:addChatLine(message.from, message.text)
	end
	print(("[chat] <%s> %s"):format(message.from, message.text))
end

local handlers = {
	welcome = onWelcome,
	snapshot = onSnapshot,
	chat = onChat,
	playerJoined = function(message)
		local player = message.player
		if player and player.id ~= state.playerId then
			state.remote[player.id] = {
				position = player.position,
				velocity = player.velocity or { 0, 0, 0 },
				yaw = player.yaw or 0,
				onGround = player.onGround or false,
				appearance = player.appearance,
			}
		end
	end,
	playerLeft = function(message)
		state.remote[message.id] = nil
		local avatar = state.avatars[message.id]
		if avatar then
			avatar:Destroy()
			state.avatars[message.id] = nil
		end
	end,
	respawned = function(message)
		state.local_.position = { message.position[1], message.position[2], message.position[3] }
		state.local_.velocity = { 0, 0, 0 }
	end,
	correction = function(message)
		state.local_.position = { message.position[1], message.position[2], message.position[3] }
		state.local_.velocity = { 0, 0, 0 }
	end,
	pong = function(message)
		link:onPong(message)
	end,
	hello = function()
		local look = Config.appearance or {}
		link:send({
			type = "join",
			appearance = {
				username = look.username or Config.playerName,
				bodyColor = look.bodyColor or look.body_color,
				shirtColor = look.shirtColor or look.shirt_color,
				pantsColor = look.pantsColor or look.pants_color,
				hat = look.hat,
			},
		})
	end,
	error = function(message)
		print("[Loomia] erro do servidor: " .. tostring(message.message))
	end,
}

-- ---------------------------------------------------------------------------
-- Loop
-- ---------------------------------------------------------------------------

local lastFrame = os.clock()

local function renderFrame(dt)
	-- 1. Rede (não bloqueia).
	link:poll()

	-- 2. Previsão local + envio de inputs a 30 Hz.
	if state.ready then
		state.accumulator = state.accumulator + dt
		local steps = 0
		while state.accumulator >= TICK_DT and steps < 4 do
			state.accumulator = state.accumulator - TICK_DT
			steps = steps + 1
			local moveX, moveZ = readMoveAxes()
			if state.chatOpen then
				moveX, moveZ = 0, 0
			end
			local jump = false
			if not state.chatOpen and state.keys.Space then
				jump = true
			end
			state.sequence = state.sequence + 1
			local input = { moveX = moveX, moveZ = moveZ, jump = jump }
			state.local_ = Physics.step(state.local_, input, state.blocks, state.minY, state.maxY, TICK_DT)
			if state.firstPerson then
				-- Em 1ª pessoa o corpo acompanha a câmera mesmo parado.
				state.local_.yaw = state.orbit.yaw
			end
			link:send({
				type = "input",
				-- O `InputState` do servidor (serde sem `rename_all`) espera
				-- `move_x`/`move_z` em snake_case.
				state = { seq = state.sequence, move_x = moveX, move_z = moveZ, jump = jump },
				cameraYaw = state.orbit.yaw,
				cameraPitch = state.orbit.pitch,
			})
		end

		if Physics.fellOut(state.local_, state.minY) then
			state.sequence = state.sequence + 1
			link:send({ type = "respawn" })
		end
	end

	-- 3. Avatares.
	local localId = state.playerId
	if localId and state.ready then
		local mine = state.avatars[localId]
		if not mine then
			local look = Config.appearance or { username = Config.playerName }
			mine = Avatar.new(look)
			state.avatars[localId] = mine
		end
		local speed = math.sqrt(
			state.local_.velocity[1] * state.local_.velocity[1]
				+ state.local_.velocity[3] * state.local_.velocity[3]
		)
		mine:update(dt, state.local_, speed)
	end

	for id, remote in pairs(state.remote) do
		local avatar = state.avatars[id]
		if not avatar then
			avatar = Avatar.new(remote.appearance or { username = "Jogador" })
			state.avatars[id] = avatar
		end
		-- Velocidade horizontal estimada a partir da diferença entre snapshots.
		local previous = state.previous[id]
		local estimated = 0
		if previous then
			local dx = remote.position[1] - previous[1]
			local dz = remote.position[3] - previous[2]
			estimated = math.sqrt(dx * dx + dz * dz) * TICK_RATE
		end
		state.previous[id] = { remote.position[1], remote.position[3] }
		local smoothed = (state.speeds[id] or 0) * 0.7 + estimated * 0.3
		state.speeds[id] = smoothed
		avatar:update(dt, remote, smoothed)
	end

	-- 4. Câmera.
	updateCamera()

	-- 5. HUD.
	if hud then
		local fps = GPU.FrameStats().FPS or 0
		local ping = link.pingMs and ("%d ms"):format(link.pingMs) or "—"
		local status = link:isConnected() and "online" or "desconectado"
		local text = string.format("FPS %d   ping %s   %s   %d parts", math.floor(fps), ping, status, state.worldParts and state.worldParts.count or 0)
		if state.chatOpen then
			text = text .. "   [chat] " .. state.chatBuffer .. "_"
		end
		hud:setStats(text)
	end
end

local function _teardown()
	if state.worldParts then
		state.worldParts:Destroy()
	end
	for _, avatar in pairs(state.avatars) do
		avatar:Destroy()
	end
	if hud then
		hud:Destroy()
	end
	if link then
		link:Close()
	end
end

-- ---------------------------------------------------------------------------
-- Boot
-- ---------------------------------------------------------------------------

bindInput()
hud = Hud.new()

link = Link.new(Config.server, handlers)

Process.Heartbeat:Connect(function(dt)
	local now = os.clock()
	local frameDt = math.min(now - lastFrame, 0.1)
	lastFrame = now
	local _ = dt
	renderFrame(frameDt)
end)

-- Espera a conexão abrir (só aqui; depois o loop cuida sozinho).
link:waitConnected(15)
if not link:isConnected() then
	print("[Loomia] não consegui conectar em " .. Config.server .. ": " .. tostring(link.lastError))
end

-- Ping a cada segundo.
Task.Spawn(function()
	while true do
		Task.Wait(1)
		if link:isConnected() then
			link:ping()
		end
	end
end)

-- `Window.Closed` ainda não existe na engine; o loop encerra junto com a janela.
