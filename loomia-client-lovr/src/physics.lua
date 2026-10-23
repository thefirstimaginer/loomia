-- Física do personagem no cliente — ESPELHA loomia-gameserver/physics.go.
--
-- O cliente prevê o movimento local com este código e reconcilia com a
-- posição autoritativa do servidor. Se este arquivo mudar, mude o de lá
-- também (as constantes batem com `CharacterRadius`, `MoveSpeed`, etc.).

local physics = {}

physics.TICK_RATE = 30
physics.TICK_DT = 1 / physics.TICK_RATE
physics.MOVE_SPEED = 12.5
physics.JUMP_SPEED = 40.0
physics.GRAVITY = -95.0
physics.MAX_FALL_SPEED = -90.0
physics.CHARACTER_RADIUS = 0.55
physics.CHARACTER_HEIGHT = 3.0
physics.STEP_HEIGHT = 0.6
physics.SNAP_DISTANCE = 0.55

local function clamp(value, lo, hi)
  if value < lo then return lo end
  if value > hi then return hi end
  return value
end

local function overlaps(box, x, y, z, radius)
  local minX, maxX = box.minX, box.maxX
  local minZ, maxZ = box.minZ, box.maxZ
  if x + radius <= minX or x - radius >= maxX then return false end
  if z + radius <= minZ or z - radius >= maxZ then return false end
  if y >= box.maxY or y + physics.CHARACTER_HEIGHT <= box.minY then return false end
  return true
end

local function solidAt(blocks, x, y, z)
  local radius = physics.CHARACTER_RADIUS
  for index = 1, #blocks do
    if overlaps(blocks[index], x, y, z, radius) then return true end
  end
  return false
end

local function highestBelow(blocks, x, y, z, probe)
  local radius = physics.CHARACTER_RADIUS - 0.01
  local best, found = 0, false
  for index = 1, #blocks do
    local box = blocks[index]
    if not (x + radius <= box.minX or x - radius >= box.maxX) then
      if not (z + radius <= box.minZ or z - radius >= box.maxZ) then
        if box.maxY <= y + 0.02 and box.maxY >= y - probe then
          if not found or box.maxY > best then
            best = box.maxY
            found = true
          end
        end
      end
    end
  end
  return best, found
end

--- Estado inicial do personagem (pés na posição de spawn).
function physics.newState(position)
  return {
    position = { position[1], position[2], position[3] },
    velocity = { 0, 0, 0 },
    yaw = 0,
    onGround = false,
  }
end

--- Um tique de física. Devolve um NOVO estado (não altera `state`).
function physics.step(state, input, blocks, minY, dt)
  local s = {
    position = { state.position[1], state.position[2], state.position[3] },
    velocity = { state.velocity[1], state.velocity[2], state.velocity[3] },
    yaw = state.yaw or 0,
    onGround = state.onGround or false,
  }

  local moveX = clamp(input.moveX or 0, -1, 1)
  local moveZ = clamp(input.moveZ or 0, -1, 1)
  local length = math.sqrt(moveX * moveX + moveZ * moveZ)
  if length > 1 then
    moveX, moveZ = moveX / length, moveZ / length
  end
  s.velocity[1] = moveX * physics.MOVE_SPEED
  s.velocity[3] = moveZ * physics.MOVE_SPEED
  if length > 0.01 then
    s.yaw = math.atan(moveX, moveZ)
  end

  local wasGrounded = s.onGround
  s.onGround = false
  if input.jump and wasGrounded then
    s.velocity[2] = physics.JUMP_SPEED
  else
    s.velocity[2] = s.velocity[2] + physics.GRAVITY * dt
    if s.velocity[2] < physics.MAX_FALL_SPEED then
      s.velocity[2] = physics.MAX_FALL_SPEED
    end
  end

  local px, py, pz = s.position[1], s.position[2], s.position[3]
  local vx, vy, vz = s.velocity[1], s.velocity[2], s.velocity[3]

  -- Horizontal, eixo a eixo (deslizar nas paredes).
  if not solidAt(blocks, px + vx * dt, py, pz) then
    px = px + vx * dt
  else
    local ground, ok = highestBelow(blocks, px + vx * dt, py + physics.STEP_HEIGHT, pz, physics.STEP_HEIGHT)
    if ok and not solidAt(blocks, px + vx * dt, ground, pz) then
      px = px + vx * dt
      py = ground
    else
      vx = 0
    end
  end
  if not solidAt(blocks, px, py, pz + vz * dt) then
    pz = pz + vz * dt
  else
    local ground, ok = highestBelow(blocks, px, py + physics.STEP_HEIGHT, pz + vz * dt, physics.STEP_HEIGHT)
    if ok and not solidAt(blocks, px, ground, pz + vz * dt) then
      pz = pz + vz * dt
      py = ground
    else
      vz = 0
    end
  end

  -- Vertical.
  local nextY = py + vy * dt
  if vy <= 0 then
    local ground, ok = highestBelow(blocks, px, py, pz, py - nextY + 0.02)
    if ok then
      nextY = ground
      vy = 0
      s.onGround = true
    end
  elseif solidAt(blocks, px, nextY, pz) then
    vy = 0
    nextY = py
  end
  py = nextY

  if not s.onGround and math.abs(vy) < 1.0 then
    local ground, ok = highestBelow(blocks, px, py, pz, physics.SNAP_DISTANCE)
    if ok then
      py = ground
      vy = 0
      s.onGround = true
    end
  end

  s.position[1], s.position[2], s.position[3] = px, py, pz
  s.velocity[1], s.velocity[2], s.velocity[3] = vx, vy, vz
  return s
end

function physics.fellOut(state, minY)
  return state.position[2] < minY
end

return physics
