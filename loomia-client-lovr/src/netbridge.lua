-- Processo auxiliar de rede: ponte UDP ↔ linhas JSON.
--
-- O LOVR não expõe UDP, então o cliente abre este script com `io.popen` e
-- fala com ele por stdin/stdout (uma mensagem JSON por linha):
--
--   lua src/netbridge.lua 127.0.0.1 7801
--
-- Livraria `socket` (LuaSocket) é opcional: sem ela, este bridge informa o
-- erro e o cliente mostra "falha de rede" no HUD.

local host = assert(arg and arg[1], 'uso: lua netbridge.lua <host> <port>')
local port = tonumber(arg and arg[2] or '') or 7801

local ok, socket = pcall(require, 'socket')
if not ok then
  -- Tenta a API de módulo do Lua 5.3 (socket.core).
  ok, socket = pcall(require, 'socket.core')
end

local stdout = io.stdout
local startClock = os.clock()

local function sendLine(text)
  stdout:write(text, '\n')
  stdout:flush()
end

if not ok or not socket then
  sendLine('{"t":"error","message":"LuaSocket ausente: instale com luarocks install luasocket"}')
  os.exit(1)
end

-- Se houver `cjson`, usa; senão, embutimos um JSON mínimo (só o necessário).
local hasCjson, cjson = pcall(require, 'cjson')
-- `json.lua` fica ao lado deste script (caminho relativo à pasta do projeto).
package.path = '../src/?.lua;' .. package.path

local function encode(value)
  if hasCjson then return cjson.encode(value) end
  return require('json').encode(value)
end

local function decode(value)
  if hasCjson then return cjson.decode(value) end
  return require('json').decode(value)
end

local udp = assert(socket.udp())
if udp:setpeername(host, port) ~= 1 then
  sendLine('{"t":"error","message":"não foi possível conectar em ' .. host .. ':' .. port .. '"}')
  os.exit(1)
end
udp:settimeout(0)

-- Contrato do loop: quando o stdin fecha (o cliente morreu), o bridge sai.
-- `socket.select` espera stdin e o socket UDP ao mesmo tempo: ler stdin com
-- `io.read` dentro de uma coroutine bloquearia o processo inteiro (não há
-- threads em Lua puro).
local running = true

local function drainStdin()
  while running do
    local line = io.stdin:read('l')
    if line == nil or line == '' then
      running = false
      return
    end
    udp:send(line)
    -- Eco do envio (o cliente usa isso para medir o ping).
    sendLine(encode({ t = 'sent', time = os.clock() - startClock }))
  end
end

while running do
  -- Espera até 1 ms por dados no socket ou em stdin (sem girar a CPU).
  local readable = socket.select({ udp }, { io.stdin }, 0.001)
  local stdinReady = false
  for _, entry in ipairs(readable) do
    if entry == io.stdin then stdinReady = true end
  end

  if stdinReady then drainStdin() end

  while true do
    local data, err = udp:receive(65535)
    if not data then
      if err ~= 'timeout' then
        sendLine(encode({ t = 'error', message = 'udp: ' .. tostring(err) }))
        running = false
      end
      break
    end
    sendLine(data)
  end
end

udp:close()
