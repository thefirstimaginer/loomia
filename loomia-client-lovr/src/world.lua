-- Carrega o mapa do master e instancia os modelos.
--
-- O `welcome` da sala traz só id/versão do mapa (UDP é pequeno demais). Aqui
-- o `.loom` vem por HTTP:
--
--     GET <origin>/api/maps/<id>   (com cookie quando logado)

local world = {}
world.__index = world

local function rgbColor(color)
  return lovr.math.newVec3((color[1] or 200) / 255, (color[2] or 200) / 255, (color[3] or 200) / 255)
end

local function decodeJson(text)
  local ok, value = pcall(lovr.data.decode, 'json', text)
  if ok and type(value) == 'table' then return value end
  local okFallback, fallback = pcall(require('src.json').decode, text)
  if okFallback and type(fallback) == 'table' then return fallback end
  return nil
end

--- Sanidade do mapa antes de gastar tempo montando modelos.
local function validate(map)
  if type(map) ~= 'table' then return 'o servidor não devolveu um JSON de mapa' end
  if type(map.blocks) ~= 'table' then return 'o mapa não tem a lista de blocos' end
  if #map.blocks == 0 then return 'o mapa está sem blocos' end
  if type(map.spawn) ~= 'table' or #map.spawn < 3 then
    map.spawn = { 0, 2, 0 } -- spawn ausente não impede jogar
  end
  return nil
end

--- Baixa o mapa (bloqueante; chamado só na tela de carregamento).
function world.fetch(origin, mapId, token)
  if not origin or origin == '' then
    return nil, 'sem endereço do servidor (origin) para baixar o mapa'
  end
  if not mapId or mapId == '' then
    return nil, 'o link não indicou qual o mapa'
  end

  local url = ('%s/api/maps/%s'):format(origin:gsub('/+$', ''), tostring(mapId))

  -- `--fail`: um 404/500 vira erro de verdade (antes o corpo de erro era
  -- tratado como "mapa" e o cliente montava um mundo vazio).
  local quote = '"'
  local parts = {
    'curl', '-sS', '--fail', '-m', '20',
    '-H', quote .. 'accept: application/json' .. quote,
  }
  if token and token ~= '' then
    parts[#parts + 1] = '-H'
    parts[#parts + 1] = quote .. 'cookie: ' .. token:gsub('"', '') .. quote
  end
  parts[#parts + 1] = quote .. url .. quote

  local handle = io.popen(table.concat(parts, ' '))
  if not handle then
    return nil, 'não foi possível executar o curl para baixar o mapa'
  end
  local text = handle:read('*a') or ''
  handle:close()

  if text == '' then
    return nil, ('o servidor não devolveu o mapa (%s)'):format(url)
  end

  local map = decodeJson(text)
  if not map then
    local short = text:sub(1, 120):gsub('%s+', ' ')
    return nil, ('resposta inválida do servidor: %s'):format(short)
  end
  if map.error then
    return nil, tostring(map.error)
  end

  local problem = validate(map)
  if problem then return nil, problem end

  map.id = map.id or mapId
  return map
end

--- Cria o mundo (modelos em lote) a partir do JSON do mapa.
function world.new(map)
  local self = setmetatable({}, world)
  self.id = map.id
  self.name = map.name or map.id
  self.spawn = map.spawn or { 0, 1, 0 }
  self.blocks = {}
  self.model = lovr.graphics.newModel()
  self.minY = 0

  local sky = map.sky_color or { 150, 200, 240 }
  lovr.graphics.setBackgroundColor(sky[1] / 255, sky[2] / 255, sky[3] / 255)

  local first = true
  local count = 0
  for _, block in ipairs(map.blocks or {}) do
    local position = block.position or { 0, 0, 0 }
    local size = block.size or { 1, 1, 1 }
    local color = block.color or { 200, 200, 200 }

    local node = self.model:addMesh(lovr.graphics.newBox(size[1], size[2], size[3]))
    node:setPosition(position[1], position[2], position[3])
    node:setMaterial(lovr.graphics.newMaterial(rgbColor(color), block.material == 'neon' and 0.4 or 1))
    if block.material == 'neon' then
      node:setEmissive(0.6)
    end

    if block.collidable ~= false then
      local halfX, halfY, halfZ = size[1] * 0.5, size[2] * 0.5, size[3] * 0.5
      self.blocks[#self.blocks + 1] = {
        minX = position[1] - halfX,
        maxX = position[1] + halfX,
        minY = position[2] - halfY,
        maxY = position[2] + halfY,
        minZ = position[3] - halfZ,
        maxZ = position[3] + halfZ,
      }
      if first or (position[2] - halfY) < self.minY then
        self.minY = position[2] - halfY
        first = false
      end
    end
    count = count + 1
  end
  self.minY = self.minY - 40
  self.blockCount = count
  return self
end

function world:draw(pass)
  pass:draw(self.model, 1, 1, 1, 1)
end

return world
