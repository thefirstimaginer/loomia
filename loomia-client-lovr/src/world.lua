-- Carrega o mapa do master server e instancia os modelos no LOVR.
--
-- O `welcome` da sala traz apenas o id/versão do mapa (UDP é pequeno demais
-- para o mapa). O cliente busca o `.loom` completo no master:
--
--     GET <origin>/api/maps/<id>
--     {"id":"baseplate","name":"...","spawn":[0,0.2,4],"sky_color":[150,200,240],
--      "blocks":[{"position":[x,y,z],"size":[w,h,d],"color":[r,g,b],"material":"grass"}]}

local world = {}
world.__index = world

local function rgbColor(color)
  return lovr.math.newVec3((color[1] or 200) / 255, (color[2] or 200) / 255, (color[3] or 200) / 255)
end

local function requestMap(origin, mapId)
  if not origin or not mapId then return nil, 'sem origem para buscar o mapa' end
  origin = origin:gsub('/$', '')
  return ('%s/api/maps/%s'):format(origin, mapId)
end

--- Baixa o mapa (bloqueante, usado só no "loading").
function world.fetch(origin, mapId)
  local url, err = requestMap(origin, mapId)
  if not url then return nil, err end

  -- `util` do LOVR não faz mídia em Lua; usamos o processo auxiliar `curl`
  -- (ou `wget`) e lemos o JSON do disco temporário.
  local path = lovr.filesystem.getSaveDirectory() .. '/world_' .. mapId .. '.loom'
  local command = ('curl -fsSL "%s" -o "%s" 2>/dev/null || wget -q -O "%s" "%s"'):format(url, path, path, url)
  local handle = io.popen(command)
  if handle then handle:read('*a') handle:close() end

  local file = io.open(path, 'r')
  if not file then return nil, 'não foi possível baixar o mapa (' .. url .. ')' end
  local text = file:read('*a')
  file:close()

  local ok, decoded = pcall(lovr.data.decode, 'json', text)
  if not ok then
    ok, decoded = pcall(require('src.json').decode, text)
  end
  if not ok or type(decoded) ~= 'table' then
    return nil, 'mapa inválido recebido do master'
  end
  return decoded
end

--- Cria o mundo (modelos em lote) a partir do JSON do mapa.
function world.new(map)
  local self = setmetatable({}, world)
  self.id = map.id
  self.name = map.name or map.id
  self.spawn = map.spawn or { 0, 1, 0 }
  self.blocks = {}       -- colliders para a física (min/max)
  self.model = lovr.graphics.newModel()
  self.minY = 0

  local sky = map.sky_color or { 150, 200, 240 }
  lovr.graphics.setBackgroundColor(sky[1] / 255, sky[2] / 255, sky[3] / 255)

  local first = true
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
  end
  self.minY = self.minY - 40
  self.blockCount = #(map.blocks or {})
  return self
end

function world:draw(pass)
  pass:draw(self.model, 1, 1, 1, 1)
end

return world
