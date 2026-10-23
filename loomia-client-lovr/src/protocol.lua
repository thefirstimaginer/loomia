-- Mensagens do protocolo Loomia (lado cliente).
--
-- Fonte única: docs/protocolo.md e loomia-gameserver/protocol_golden.json.
-- O engine chama isto de `loomia.protocol` (main.lua faz `require('src.protocol')`).

local json = require('src.json')

local protocol = {}

protocol.VERSION = 1

--- Serializa uma mensagem para o fio.
function protocol.encode(message)
  local ok, encoded = pcall(function()
    return lovr.data.encode(message, 'json')
  end)
  if ok and encoded then return encoded end
  return json.encode(message)
end

--- Lê uma mensagem recebida (devolve nil se não for um objeto JSON válido).
function protocol.decode(text)
  if type(text) ~= 'string' or text == '' then return nil end
  local ok, message = pcall(function()
    return lovr.data.decode('json', text)
  end)
  if not ok or type(message) ~= 'table' then
    ok, message = pcall(json.decode, text)
  end
  if not ok or type(message) ~= 'table' or type(message.t) ~= 'string' then
    return nil
  end
  return message
end

-- ---------------------------------------------------------------- client → servidor

function protocol.hello(token, name, appearance)
  return { t = 'hello', token = token, name = name, appearance = appearance }
end

function protocol.input(seq, moveX, moveZ, jump, yaw)
  return {
    t = 'input',
    seq = seq,
    move_x = moveX,
    move_z = moveZ,
    jump = jump and true or false,
    yaw = yaw,
  }
end

function protocol.chat(text)
  return { t = 'chat', text = text }
end

function protocol.ping(id, time)
  return { t = 'ping', id = id, time = time }
end

function protocol.respawn()
  return { t = 'respawn' }
end

function protocol.bye()
  return { t = 'bye' }
end

-- ---------------------------------------------------------------- servidor → cliente

--- Normaliza o `welcome`: devolve id, spawn, jogadores e histórico do chat.
function protocol.parseWelcome(message)
  local players = {}
  for _, player in ipairs(message.players or {}) do
    players[#players + 1] = {
      id = player.id,
      name = player.name or 'Jogador',
      appearance = player.appearance,
      position = player.p or message.spawn or { 0, 0, 0 },
      yaw = player.yaw or 0,
    }
  end
  return {
    id = message.id,
    spawn = message.spawn or { 0, 0, 0 },
    tickRate = message.tick_rate or 30,
    mapId = message.map_id,
    mapName = message.map_name,
    worldVersion = message.world_version,
    skyColor = message.sky_color,
    players = players,
    chat = message.chat or {},
  }
end

--- Lê um snapshot. IMPORTANTE: o snapshot não traz o próprio jogador nem um
--- `ack` de topo — o ack é `players[].ack` da entrada com o id local.
function protocol.parseSnapshot(message, localId)
  local ack = 0
  local others = {}
  for _, entry in ipairs(message.players or {}) do
    if entry.id == localId then
      ack = entry.ack or ack
      -- (a posição do próprio jogador é autoritativa para a reconciliação)
    else
      others[#others + 1] = entry
    end
  end
  return { tick = message.tick or 0, ack = ack, players = others, entries = message.players or {} }
end

return protocol
