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
	width = 1080,
	height = 720,
})
local camera = win:GetViewport()
camera.FOV = 70
camera.Near = 0.1
camera.Far = 2000

GPU.SetMaxFrameRate(30)

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
	orbit = { yaw = 0, pitch = -0.38, distance = 11 },
	avatars = {},      -- [player_id] = Avatar
	remote = {},       -- [player_id] = { position, yaw, velocity, appearance }
	previous = {},     -- [player_id] = posição anterior (para estimar velocidade)
	speeds = {},       -- [player_id] = velocidade horizontal suavizada
	firstPerson = false,
	cameraDragging = false,
	chatHint = nil,
	pendingInputs = {},
	chatOpen = false,
	chatBuffer = "",
	chatLog = {},      -- fila de mensagens digitadas (envio com Enter)
	pingTick = 0,
	startedAt = os.clock(),
	-- Diferença acumulada entre previsão local e servidor (aplicada suave).
	correction = { 0, 0, 0 },
}

local link = nil
local hud = nil

-- ---------------------------------------------------------------------------
-- Câmera orbital
-- ---------------------------------------------------------------------------

--- CFrame do personagem (posição + yaw). O rosto dele está em +Z.
local function playerCFrame(position, yaw)
	return CFrame.new(
		Vector.new(position[1], position[2], position[3]),
		Vector.new(0, yaw or 0, 0)
	)
end

--- Yaw da câmera: só o mouse muda (é ele que define "frente" para o WASD).
state.cameraYaw = 0

local function cameraCFrame()
	local pos = state.local_.position

	-- Ponto de foco: altura dos olhos do personagem.
	local target = Vector.new(
		pos[1],
		pos[2] + EYE_HEIGHT,
		pos[3]
	)

	if state.firstPerson then
		-- 1ª pessoa: a câmera fica nos olhos e o mouse gira livremente
		-- (inclusive para cima).
		local pitch = math.max(-1.20, math.min(1.20, state.orbit.pitch))
		local dir = Vector.new(
			math.sin(state.cameraYaw) * math.cos(pitch),
			-math.sin(pitch),
			math.cos(state.cameraYaw) * math.cos(pitch)
		)
		return CFrame.LookAt(target, target + dir, Vector.new(0, 1, 0))
	end

	-- 3ª pessoa: a câmera fica ATRÁS do personagem, mas o seu yaw é PRÓPRIO
	-- (`state.cameraYaw`, controlado pelo mouse). O corpo gira para onde anda
	-- sem arrastar a câmera junto — se a câmera seguisse o yaw do corpo, o
	-- "D" mudaria de direção a cada tique e o boneco andaria em círculo.
	local distance = math.max(3, math.min(24, state.orbit.distance))
	local pitch = math.max(-0.45, math.min(-0.05, state.orbit.pitch))
	local horizontal = math.cos(pitch) * distance
	local cameraYaw = state.cameraYaw

	-- Offset atrás do personagem; pitch negativo sobe a câmera.
	local eye = target + Vector.new(
		-math.sin(cameraYaw) * horizontal,
		-math.sin(pitch) * distance,
		-math.cos(cameraYaw) * horizontal
	)

	return CFrame.LookAt(eye, target, Vector.new(0, 1, 0))
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
    -- O mouse gira a CÂMERA (não o corpo). Em 1ª pessoa ele também controla o
    -- A câmera só gira enquanto o jogador ARRASTA com o botão direito (ou
    -- esquerdo em 1ª pessoa). Passar o cursor na tela não move mais nada.
    Mouse.Moved:Connect(function(_pos, delta)
    	if state.chatOpen or not state.cameraDragging then
    		return
    	end
    	state.cameraYaw = state.cameraYaw - delta.X * 0.005
    	if state.firstPerson then
    		state.orbit.pitch = math.max(-1.20, math.min(1.20, state.orbit.pitch - delta.Y * 0.005))
    	else
    		state.orbit.pitch = math.max(-0.45, math.min(-0.05, state.orbit.pitch - delta.Y * 0.003))
    	end
    end)

    Mouse.InputReceived:Connect(function(button, kind)
    	local down = kind == "Begin"
    	if button == "MouseButton2" or (button == "MouseButton1" and state.firstPerson) then
    		state.cameraDragging = down
    		if state.firstPerson and down then
    			pcall(function()
    				Mouse.Lock()
    			end)
    		elseif state.firstPerson and not down then
    			pcall(function()
    				Mouse.Unlock()
    			end)
    		end
    	end
    end)

    Keyboard.InputChanged:Connect(function(_id, key, kind)
        local name = key
        -- Garante que o WASD e Espaço atualizem o estado contínuo das teclas
        if name == "W" or name == "A" or name == "S" or name == "D" or name == "Space" then
            state.keys[name] = (kind == "Begin")
            return
        end
        if kind ~= "Begin" then
            return
        end
        if name == "Escape" then
            state.chatOpen = false
            state.cameraDragging = false
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
            state.chatBuffer = ""
            state.chatHint = "Enter envia  ·  Esc cancela  ·  Backspace apaga"
        elseif name == "R" then
            link:send({ type = "respawn" })
        elseif name == "V" then
        	state.firstPerson = not state.firstPerson
        	if state.firstPerson then
        		-- Entra em 1ª pessoa olhando para onde o corpo já aponta.
        		state.cameraYaw = state.local_.yaw or 0
        		state.orbit.pitch = 0
        	else
        		-- Volta para 3ª pessoa a uma distância visível e atrás do corpo.
        		state.orbit.distance = 11
        		state.orbit.pitch = -0.38
        	end
        end
    end)

    -- Zoom com a roda do mouse
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
	-- Eixos relativos à CÂMERA (`cameraYaw`), que só muda com o mouse. O corpo
	-- gira para onde anda (o Physics faz isso), mas esse giro NÃO entra aqui —
	-- senão o "D" mudaria de direção a cada tique.
	-- Na Ruzit o forward é +Z: (sin yaw, 0, cos yaw); a direita é (cos yaw, 0, -sin yaw).
	local yaw = state.cameraYaw
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

	-- Iluminação: a engine não liga sol/ambiente por padrão e as peças usam
	-- `Lit = true`, então sem isso o mundo sai preto.
	local LightingService = import("LightingService")
	local Primitives = import("Primitives")
	LightingService.SetSun(Vector.new(-0.45, -0.8, -0.35), Primitives.Color3.new(1.0, 0.97, 0.90))
	LightingService.SetAmbient(Primitives.Color3.new(0.42, 0.45, 0.52))

	-- Céu do mapa (shader de gradiente) — sem ele o fundo é preto.
	Map.applySky(world)

	print(string.format("[Loomia] mundo '%s': %d blocos → %d parts", world.name or world.id or "?", #state.blocks, parts.count))
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

	-- No boot a câmera já nasce atrás do personagem (3ª pessoa), olhando na
	-- O `Welcome` não define a direção: a câmera começa olhando para o spawn
	-- (+Z) e o corpo já nasce virado para lá.
	state.cameraYaw = state.local_.yaw or 0
	state.orbit.pitch = -0.38
	state.orbit.distance = 11
	state.firstPerson = false

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
			-- Reconciliação. NÃO mexer na posição de forma brusca: o servidor
			-- aplica os inputs em lotes (as vezes 2 num tique e 0 no outro), o
			-- que criava um vaivem visível ("puxão"). Guardamos a DIFERENÇA e
			-- amortizamos ao longo de alguns frames no loop.
			local a = player.position
			local p = state.local_.position
			local dx, dz = p[1] - a[1], p[3] - a[3]
			local dy = p[2] - a[2]
			local drift = math.sqrt(dx * dx + dz * dz)

			-- Reconciliação por REPLAY (como no netcode de FPS):
			--   1. volta para a posição autoritativa do servidor;
			--   2. reaplica os inputs que o servidor ainda não confirmou (seq > ack).
			-- Assim o cliente nunca fica à frente nem atrás do servidor — o que
			-- antes fazia o boneco "escorregar" para fora do mapa.
			local ack = player.ack or 0
			local pending = {}
			for _, entry in ipairs(state.pendingInputs) do
				if entry.seq > ack then
					pending[#pending + 1] = entry
				end
			end
			state.pendingInputs = pending

			local predicted = {
				position = { a[1], a[2], a[3] },
				velocity = { player.velocity[1], player.velocity[2], player.velocity[3] },
				yaw = p.yaw,
				onGround = player.onGround,
			}
			for _, entry in ipairs(pending) do
				predicted = Physics.step(
					predicted,
					{ moveX = entry.moveX, moveZ = entry.moveZ, jump = entry.jump },
					state.blocks, state.minY, state.maxY, TICK_DT
				)
			end

			if drift > 6.0 or math.abs(dy) > 6.0 then
				-- Teleporte real (respawn): aceita direto, sem reaplicar nada.
				state.local_.position = { a[1], a[2], a[3] }
				state.local_.velocity = { player.velocity[1], player.velocity[2], player.velocity[3] }
				state.local_.onGround = player.onGround
				state.pendingInputs = {}
			else
				-- A posição final já é a previsão correta; apenas a suavizamos
				-- visualmente para não haver solavancos.
				state.local_.position = predicted.position
				state.local_.velocity = predicted.velocity
			end
			state.correction = { 0, 0, 0 }
			state.local_.onGround = state.local_.onGround or player.onGround
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


local function renderFrame(dt)
	-- 1. Rede (não bloqueia).
	link:poll()

	-- 2. Previsão local + envio de inputs a 30 Hz, no máximo UM por tique do
	-- servidor. Sem esse limite o cliente enviava ~1,5 input por tique: o
	-- boneco adiantava e o servidor o puxava de volta (o "puxão").
	if state.ready then
		state.accumulator = state.accumulator + dt
		local steps = 0
		while state.accumulator >= TICK_DT and steps < 1 do
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
				state.local_.yaw = state.cameraYaw
			end
			link:send({
				type = "input",
				-- O `InputState` do servidor (serde sem `rename_all`) espera
				-- `move_x`/`move_z` em snake_case.
				state = { seq = state.sequence, move_x = moveX, move_z = moveZ, jump = jump },
				cameraYaw = state.cameraYaw,
				cameraPitch = state.orbit.pitch,
			})
			-- Guarda o input para reaplicar na reconciliação (até o servidor
			-- confirmar esse `seq` no `ack`).
			state.pendingInputs[#state.pendingInputs + 1] = {
				seq = state.sequence, moveX = moveX, moveZ = moveZ, jump = jump,
			}
			if #state.pendingInputs > 120 then
				table.remove(state.pendingInputs, 1)
			end
		end

		if Physics.fellOut(state.local_, state.minY) then
			state.sequence = state.sequence + 1
			link:send({ type = "respawn" })
		end

		-- Aplica a correção do servidor aos poucos (não no meio do tique), para
		-- o boneco não dar solavancos ao reconciliar.
		local corr = state.correction
		local amount = math.min(1, dt * 12)
		local moved = math.abs(corr[1]) + math.abs(corr[2]) + math.abs(corr[3])
		if moved > 1e-4 then
			state.local_.position[1] = state.local_.position[1] + corr[1] * amount
			state.local_.position[2] = state.local_.position[2] + corr[2] * amount
			state.local_.position[3] = state.local_.position[3] + corr[3] * amount
			corr[1] = corr[1] * (1 - amount)
			corr[2] = corr[2] * (1 - amount)
			corr[3] = corr[3] * (1 - amount)
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
		-- O avatar local cola na posição prevista (`snap`), mas a animação usa o
		-- tempo REAL do frame — passar `dt = 1` acelerava a passada ~60x.
		mine:update(dt, state.local_, speed, { snap = true })
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

	-- 4. Câmera: em 3ª pessoa segue o personagem, mas o yaw é independente
	-- (controlado só pelo mouse). Em 1ª pessoa o corpo acompanha a câmera.
	if state.firstPerson then
		state.local_.yaw = state.cameraYaw
	end
	updateCamera()

	-- 5. HUD.
	if hud then
		local fps = GPU.FrameStats().FPS or 0
		local ping = link.pingMs and ("%d ms"):format(link.pingMs) or "—"
		local status = link:isConnected() and "online" or "desconectado"
		local text = string.format("FPS %d   ping %s   %s   %d parts", math.floor(fps), ping, status, state.worldParts and state.worldParts.count or 0)
		hud:setStats(text)
		-- Caixa "Dizendo:" do chat (só quando aberta). A dica aparece sempre
		-- que o chat abre e some depois de alguns segundos.
		local hint = state.chatHint
		state.chatHint = nil
		hud:setChatInput(state.chatOpen, state.chatBuffer, hint)
		hud:update(dt)
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

GPU.SetMaxFrameRate(60)

link = Link.new(Config.server, handlers)

-- O `dt` do Heartbeat é o tempo REAL desde o frame anterior. `os.clock()`
-- mede tempo de CPU, que num loop ocioso avança mais rápido e inflava o
-- acumulador de física (~1,5 input por tique do servidor — o boneco adiantava
-- e o servidor o "puxava" de volta).
Process.Heartbeat:Connect(function(dt)
	local frameDt = math.min(dt or (1 / 60), 0.1)
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
