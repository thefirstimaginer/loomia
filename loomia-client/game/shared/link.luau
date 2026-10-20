--[[
	Link WebSocket com o servidor do Loomia.

	O loop principal do Ruzit é síncrono por frame: `Receive` bloqueia a thread
	que o chama. Para não congelar o jogo, o socket real vive numa **thread
	separada** (Task.Spawn) e conversa com o frame através de duas filas:

		main ──(sendQueue)──► [thread WS] ── Send -------► servidor
		main ◄──(receiveQueue)◄─ [thread WS] ◄─ Receive ── servidor

	`Link:poll()` roda uma vez por frame, dispara os handlers e drena as filas,
	sem bloquear. `Link:waitConnected()` bloqueia só no boot (tela de loading).
]]

local Link = {}
Link.__index = Link

local Net = import("Net")
local Serde = import("Serde")
local Task = import("Task")

local startedAt = os.clock()

--- Relógio monotônico (equivalente ao `Instant::now()` do Bevy).
local function nowSeconds()
	return os.clock() - startedAt
end

--- @param url string  ex.: "ws://127.0.0.1:8080/ws"
function Link.new(url, handlers)
	local self = setmetatable({}, Link)
	self.url = url
	self.handlers = handlers or {}
	self.state = "connecting" -- connecting | connected | closed
	self.sendQueue = {}
	self.receiveQueue = {}
	self.inbox = nil
	self.outbox = nil
	self.lastError = nil
	self.lastReceiveAt = nowSeconds()
	self.pingSentAt = nil
	self.pingMs = nil
	self.bytesIn = 0
	self.connectedWanted = false
	self:startWorker()
	return self
end

--- Abre o socket numa thread própria e mantém o bombeamento lá.
function Link:startWorker()
	self.state = "connecting"
	-- `Net.Socket.Connect` é BLOQUEANTE: não pode rodar no script principal
	-- (travaria a janela antes do primeiro frame). A thread começa sem socket
	-- e só abre a conexão quando o frame principal faz a primeira chamada a
	-- `Link:poll()`, que marca `self.connectedWanted`.
	local function sendOutgoing()
		local outgoing = self.sendQueue
		if #outgoing == 0 then
			return true
		end
		self.sendQueue = {}
		for _, text in ipairs(outgoing) do
			local sent, sendErr = pcall(function()
				self.inbox:Send(text)
			end)
			if not sent then
				self.state = "closed"
				self.lastError = tostring(sendErr)
				return false
			end
		end
		return true
	end

	Task.Spawn(function()
		-- Espera o pedido do frame principal antes de conectar.
		while not self.connectedWanted do
			Task.Wait(0.01)
		end
		local okOpen, err = pcall(function()
			self.inbox = Net.Socket.Connect(self.url)
		end)
		if not okOpen or self.inbox == nil then
			self.state = "closed"
			self.lastError = tostring(err)
			return
		end
		self.state = "connected"
		self.lastReceiveAt = nowSeconds()

		while true do
			if not sendOutgoing() then
				return
			end

			-- Leitura com timeout de ~8ms: o laço acima roda vira e mexe, então
			-- enviar e receber ficam intercalados mesmo com o servidor quieto.
			local ok, message = pcall(function()
				return self.inbox:ReceiveTimeout(0.008)
			end)
			if not ok then
				self.state = "closed"
				self.lastError = tostring(message)
				return
			end
			if message then
				self.receiveQueue[#self.receiveQueue + 1] = message
				self.lastReceiveAt = nowSeconds()
			end

			-- Cede a vez para o loop principal rodar o frame.
			Task.Wait(0)
		end
	end)
end

--- Aguarda (no boot) até a conexão abrir ou falhar. `timeout` em segundos.
function Link:waitConnected(timeout)
	timeout = timeout or 10
	local started = nowSeconds()
	while self.state == "connecting" do
		if nowSeconds() - started > timeout then
			return false
		end
		Task.Wait(0.05)
	end
	return self.state == "connected"
end

function Link:isConnected()
	return self.state == "connected"
end

--- Envia uma tabela como JSON (vai no próximo frame).
function Link:send(message)
	if self.state ~= "connected" then
		return false
	end
	local ok, text = pcall(function()
		return Serde.Encode("json", message)
	end)
	if not ok then
		return false
	end
	self.sendQueue[#self.sendQueue + 1] = text
	return true
end

--- Processa as mensagens recebidas (chamado 1x por frame).
function Link:poll()
	-- Primeiro frame: a thread do socket pode abrir a conexão agora.
	if not self.connectedWanted then
		self.connectedWanted = true
	end
	local queue = self.receiveQueue
	if #queue == 0 then
		return
	end
	self.receiveQueue = {}
	for _, text in ipairs(queue) do
		self.bytesIn = self.bytesIn + #text
		self.lastReceiveAt = nowSeconds()
		local ok, message = pcall(function()
			return Serde.Decode("json", text)
		end)
		if ok and type(message) == "table" and type(message.type) == "string" then
			local handler = self.handlers[message.type]
			if handler then
				local okHandler, handlerErr = pcall(handler, message)
				if not okHandler then
					print("[link] erro no handler '" .. message.type .. "': " .. tostring(handlerErr))
				end
			end
		end
	end
end

--- Mede a latência com um ping e devolve a última medida (ms).
function Link:ping()
	if self.state ~= "connected" then
		return self.pingMs
	end
	self.pingSentAt = nowSeconds()
	self:send({ type = "ping", clientTimeMs = self.pingSentAt * 1000 })
	return self.pingMs
end

function Link:onPong(message)
	if self.pingSentAt then
		local sentMs = message.clientTimeMs or (self.pingSentAt * 1000)
		self.pingMs = math.max(0, nowSeconds() * 1000 - sentMs)
	end
end

function Link:Close()
	if self.inbox then
		pcall(function()
			self.inbox:Close()
		end)
	end
	self.state = "closed"
end

return Link
