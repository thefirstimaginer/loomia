-- Teste do protocolo do lado Lua.
--
-- Roda sem o engine:  lua loomia-client-lovr/tests/protocol_spec.lua
-- (também sai com código != 0 se algo divergir de protocol_golden.json.)

-- Permite `require('src.x')` a partir da raiz do projeto do cliente.
local here = (arg and arg[0] or ''):match('^(.*)/tests/[^/]+$') or '.'
package.path = here .. '/?.lua;' .. here .. '/src/?.lua;' .. package.path

local json = require('src.json')
local proto = require('src.protocol')
local physics = require('src.physics')

-- `lovr.data` não existe fora do engine: o protocolo usa o fallback `json`.
lovr = nil

local passed, failed = 0, 0

local function check(name, condition, detail)
  if condition then
    passed = passed + 1
    print(('  ok   %s'):format(name))
  else
    failed = failed + 1
    print(('  FAIL %s %s'):format(name, detail and ('— ' .. tostring(detail)) or ''))
  end
end

local function readFile(path)
  local file = assert(io.open(path, 'r'), 'não abriu ' .. path)
  local content = file:read('*a')
  file:close()
  return content
end

local goldenPath = here .. '/../loomia-gameserver/protocol_golden.json'
local ok, golden = pcall(function()
  return json.decode(readFile(goldenPath))
end)
if not ok then
  print('não consegui ler ' .. goldenPath .. ': ' .. tostring(golden))
  os.exit(2)
end

print('constantes')
for name, value in pairs(golden.constants) do
  local luaName = name:upper()
  local actual = physics[luaName]
  check(name, actual == value, ('golden %s, Lua %s'):format(value, tostring(actual)))
end

print('mensagens do servidor')
for _, raw in ipairs(golden.server_to_client) do
  local message = raw
  if type(raw) == 'string' then message = json.decode(raw) end
  check('t presente (' .. tostring(message.t) .. ')', type(message.t) == 'string' and message.t ~= '')
end
local snapshot = golden.server_to_client[2]
if type(snapshot) == 'string' then snapshot = json.decode(snapshot) end
check('snapshot não tem ack de topo', snapshot.ack == nil)
local first = snapshot.players and snapshot.players[1]
check('ack vem em players[].ack', first and first.ack == 12, first and tostring(first.ack))

print('mensagens do cliente')
for _, raw in ipairs(golden.client_to_server) do
  local message = raw
  if type(raw) == 'string' then message = json.decode(raw) end
  local encoded = proto.encode(message)
  local again = json.decode(encoded)
  check('idempotente (' .. tostring(message.t) .. ')', again.t == message.t, encoded)
end

-- O cliente monta o input exatamente como o golden.
local input = proto.input(12, 1, 0, false, 0.4)
check('input.seq', input.seq == 12)
check('input.move_x/move_z', input.move_x == 1 and input.move_z == 0)
check('input.yaw', input.yaw == 0.4)

-- O `ack` do jogador local é lido de `players[].ack`.
local snapshotForMe = proto.parseSnapshot(snapshot, 3)
check('parseSnapshot acha o ack do próprio jogador', snapshotForMe.ack == 12, snapshotForMe.ack)
local snapshotWithoutMe = proto.parseSnapshot(snapshot, 99)
check('sem entrada própria, ack fica 0', snapshotWithoutMe.ack == 0)

print(('\n%d passaram, %d falharam'):format(passed, failed))
os.exit(failed == 0 and 0 or 1)
