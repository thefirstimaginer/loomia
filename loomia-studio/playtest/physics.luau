--[[
	Física do personagem — usada pelo Playtest do Studio (e cópia fiel da do
	cliente). Port de `loomia-shared/src/physics.rs`: mesmas constantes, mesma
	colisão AABB, mesmo *sweep* por eixo e mesmo degrau automático.

	Estado do personagem:
		position = { x, y, z }  -- pés (base da hitbox)
		velocity = { x, y, z }
		yaw      = radianos
		onGround = boolean
]]

local Physics = {}

Physics.GRAVITY = -58.0
Physics.WALK_SPEED = 12.5
Physics.JUMP_VELOCITY = 21.0
Physics.GROUND_ACCEL = 90.0
Physics.AIR_ACCEL = 22.0
Physics.GROUND_FRICTION = 60.0
Physics.MAX_FALL_SPEED = -90.0
Physics.CHARACTER_RADIUS = 0.55
Physics.CHARACTER_HEIGHT = 3.0
Physics.STEP_HEIGHT = 0.6
Physics.MAX_TICK = 0.25

local EPS = 1e-4

--- Faixa vertical do mapa, usada para descartar blocos rapidamente.
function Physics.bounds(blocks)
	local minY, maxY = math.huge, -math.huge
	for _, block in ipairs(blocks) do
		local y = block.position[2]
		local half = block.size[2] * 0.5
		if y - half < minY then
			minY = y - half
		end
		if y + half > maxY then
			maxY = y + half
		end
	end
	if #blocks == 0 then
		return 0.0, 0.0
	end
	return minY, maxY
end

local function aabbOverlap(ax, ay, az, ahx, ahy, ahz, bx, by, bz, bhx, bhy, bhz)
	return ax - ahx < bx + bhx
		and ax + ahx > bx - bhx
		and ay - ahy < by + bhy
		and ay + ahy > by - bhy
		and az - ahz < bz + bhz
		and az + ahz > bz - bhz
end

--- Testa se a hitbox do personagem em (x, y, z) intersecta algum bloco sólido.
function Physics.collides(blocks, x, y, z, minY, maxY)
	-- Hitbox: [x±R, y+0.001 .. y+H, z±R]
	local cy = (y + 0.001 + y + Physics.CHARACTER_HEIGHT) * 0.5
	local hy = (Physics.CHARACTER_HEIGHT - 0.001) * 0.5
	if maxY ~= nil and (cy - hy > maxY or cy + hy < minY) then
		return false
	end
	local r = Physics.CHARACTER_RADIUS
	for _, block in ipairs(blocks) do
		if block.collidable then
			local pos, size = block.position, block.size
			if aabbOverlap(
				x, cy, z, r, hy, r,
				pos[1], pos[2], pos[3], size[1] * 0.5, size[2] * 0.5, size[3] * 0.5
			) then
				return true
			end
		end
	end
	return false
end

--- Chão logo abaixo dos pés (até `probe` unidades).
function Physics.groundBelow(blocks, x, y, z, probe)
	local r = Physics.CHARACTER_RADIUS - 0.01
	local cy = (y - probe + y + 0.02) * 0.5
	local hy = (probe - 0.02) * 0.5
	for _, block in ipairs(blocks) do
		if block.collidable then
			local pos, size = block.position, block.size
			if aabbOverlap(
				x, cy, z, r, hy, r,
				pos[1], pos[2], pos[3], size[1] * 0.5, size[2] * 0.5, size[3] * 0.5
			) then
				return true
			end
		end
	end
	return false
end

--- Cópia rasa do estado.
function Physics.clone(state)
	return {
		position = { state.position[1], state.position[2], state.position[3] },
		velocity = { state.velocity[1], state.velocity[2], state.velocity[3] },
		yaw = state.yaw,
		onGround = state.onGround,
	}
end

local function normalizedAxes(ax, az)
	ax = math.max(-1, math.min(1, ax or 0))
	az = math.max(-1, math.min(1, az or 0))
	local len = math.sqrt(ax * ax + az * az)
	if len > 1 then
		return ax / len, az / len
	end
	return ax, az
end

local function horizontalSpeed(vx, vz)
	return math.sqrt(vx * vx + vz * vz)
end

--- Um tique de física (equivalente a `move_character`). Devolve um NOVO estado.
--- `input = { moveX, moveZ, jump }` com os eixos já no espaço do mundo.
function Physics.step(state, input, blocks, minY, maxY, dt)
	dt = math.min(dt or 0, Physics.MAX_TICK)
	if dt <= 0 then
		return Physics.clone(state)
	end

	local px, py, pz = state.position[1], state.position[2], state.position[3]
	local vx, vy, vz = state.velocity[1], state.velocity[2], state.velocity[3]
	local onGround = state.onGround

	local ax, az = normalizedAxes(input.moveX, input.moveZ)

	-- Velocidade horizontal desejada (acelera em direção a ela).
	local wishX, wishZ = ax * Physics.WALK_SPEED, az * Physics.WALK_SPEED
	local accel = onGround and Physics.GROUND_ACCEL or Physics.AIR_ACCEL
	local dx, dz = wishX - vx, wishZ - vz
	local diff = math.sqrt(dx * dx + dz * dz)
	if diff > EPS then
		local stepLen = math.min(accel * dt, diff)
		vx = vx + (dx / diff) * stepLen
		vz = vz + (dz / diff) * stepLen
	elseif onGround and math.abs(ax) < EPS and math.abs(az) < EPS then
		local speed = horizontalSpeed(vx, vz)
		local drop = math.min(Physics.GROUND_FRICTION * dt, speed)
		if speed > EPS then
			vx = vx - (vx / speed) * drop
			vz = vz - (vz / speed) * drop
		else
			vx, vz = 0, 0
		end
	end

	-- Gravidade + pulo.
	if not onGround then
		vy = math.max(vy + Physics.GRAVITY * dt, Physics.MAX_FALL_SPEED)
	end
	if input.jump and onGround then
		vy = Physics.JUMP_VELOCITY
		onGround = false
	end

	-- --- Eixo X ---
	local step = vx * dt
	if step ~= 0 then
		local cx = px + step
		if Physics.collides(blocks, cx, py, pz, minY, maxY) then
			if onGround and not Physics.collides(blocks, cx, py + Physics.STEP_HEIGHT, pz, minY, maxY) then
				py = py + Physics.STEP_HEIGHT
				px = cx
			else
				vx = 0
			end
		else
			px = cx
		end
	end

	-- --- Eixo Z ---
	step = vz * dt
	if step ~= 0 then
		local cz = pz + step
		if Physics.collides(blocks, px, py, cz, minY, maxY) then
			if onGround and not Physics.collides(blocks, px, py + Physics.STEP_HEIGHT, cz, minY, maxY) then
				py = py + Physics.STEP_HEIGHT
				pz = cz
			else
				vz = 0
			end
		else
			pz = cz
		end
	end

	-- --- Eixo Y ---
	local dy = vy * dt
	if dy ~= 0 then
		local cy = py + dy
		if Physics.collides(blocks, px, cy, pz, minY, maxY) then
			if dy < 0 then
				-- Desce em passos pequenos até encostar (pouso suave).
				local landedY = py
				for i = 1, 10 do
					local probe = py + (dy / 10) * i
					if Physics.collides(blocks, px, probe, pz, minY, maxY) then
						break
					end
					landedY = probe
				end
				py = landedY
				onGround = true
			end
			vy = 0
		else
			py = cy
			onGround = dy < 0 and Physics.groundBelow(blocks, px, cy, pz, 0.05)
		end
	else
		onGround = state.onGround and Physics.groundBelow(blocks, px, py, pz, 0.06)
	end

	-- Empurrado para dentro de um bloco (spawn ruim): libera a queda.
	if Physics.collides(blocks, px, py, pz, minY, maxY) then
		onGround = false
	end

	-- O avatar encara a direção em que anda: atan2(x, z), como no servidor.
	local yaw = state.yaw
	if math.abs(ax) > 1e-3 or math.abs(az) > 1e-3 then
		yaw = math.atan2(ax, az)
	end

	return {
		position = { px, py, pz },
		velocity = { vx, vy, vz },
		yaw = yaw,
		onGround = onGround,
	}
end

--- O personagem caiu do mapa?
function Physics.fellOut(state, minY)
	return state.position[2] < (minY or 0) - 30.0
end

return Physics
