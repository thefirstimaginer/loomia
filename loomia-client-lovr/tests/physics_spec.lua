-- Teste de paridade da física: o mesmo mundo e os mesmos inputs têm que dar a
-- mesma posição no cliente (Lua) e no servidor (Go).
--
-- Gera `loomia-gameserver/physics_golden.json` com `go test -run TestWritePhysicsGolden`
-- e lê o arquivo aqui. Rode:
--   lua loomia-client-lovr/tests/physics_spec.lua

local here = (arg and arg[0] or ''):match('^(.*)/tests/[^/]+$') or '.'
package.path = here .. '/?.lua;' .. here .. '/src/?.lua;' .. package.path

local json = require('src.json')
local physics = require('src.physics')

local function readFile(path)
  local file = assert(io.open(path, 'r'), 'não abriu ' .. path)
  local content = file:read('*a')
  file:close()
  return content
end

local path = here .. '/../loomia-gameserver/physics_golden.json'
local golden = json.decode(readFile(path))

local failures = 0
for _, scenario in ipairs(golden) do
  -- Blocos do cenário, no formato que a física do cliente usa.
  local blocks = {}
  for _, block in ipairs(scenario.blocks) do
    local px, py, pz = block.position[1], block.position[2], block.position[3]
    local sx, sy, sz = block.size[1], block.size[2], block.size[3]
    if block.collidable ~= false then
      blocks[#blocks + 1] = {
        minX = px - sx / 2, maxX = px + sx / 2,
        minY = py - sy / 2, maxY = py + sy / 2,
        minZ = pz - sz / 2, maxZ = pz + sz / 2,
      }
    end
  end

  local state = {
    position = { scenario.start[1], scenario.start[2], scenario.start[3] },
    velocity = { 0, 0, 0 },
    yaw = 0,
    onGround = scenario.on_ground,
  }
  for step = 1, scenario.steps do
    local input = scenario.inputs[((step - 1) % #scenario.inputs) + 1]
    state = physics.step(state, {
      moveX = input.move_x or 0,
      moveZ = input.move_z or 0,
      jump = input.jump or false,
    }, blocks, -1000, physics.TICK_DT)
  end

  local expected = scenario.expected
  local dx = math.abs(state.position[1] - expected.position[1])
  local dy = math.abs(state.position[2] - expected.position[2])
  local dz = math.abs(state.position[3] - expected.position[3])
  local tolerance = 1e-6
  local ok = dx < tolerance and dy < tolerance and dz < tolerance
              and state.onGround == expected.on_ground
  print(('  %s %s (d=%.9f,%.9f,%.9f)'):format(ok and 'ok  ' or 'FAIL', scenario.name, dx, dy, dz))
  if not ok then failures = failures + 1 end
end

print(failures == 0 and 'física em paridade' or (failures .. ' cenários divergentes'))
os.exit(failures == 0 and 0 or 1)
