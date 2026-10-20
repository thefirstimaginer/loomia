--[[
	Loomia Studio — editor de mapas do Loomia sobre a engine Ruzit.

	Ferramentas (clicando no viewport):
		clique           colocar um bloco na face apontada
		Shift + clique   apagar o bloco apontado
		Alt + clique     pintar (cor + material atuais)
		Arrastar o bloco selecionado com LMB (ou setas do teclado) move 1 unidade
		Câmera: RMB orbita, MMB faz pan, roda do mouse aproxima.

	Arquivos:
		Ctrl+S  POST /api/maps  (o servidor grava o .loom e recarrega o mundo)
		        e também grava <mapsDir>/<id>.loom localmente
		Ctrl+L  recarrega do servidor (GET /api/maps/{id})
		Sem servidor no ar: o Studio abre uma baseplate vazia e só salva local.
]]

local Window = import("Window")
local Primitives = import("Primitives")
local Process = import("Process")
local Keyboard = import("Keyboard")
local Mouse = import("Mouse")
local GPU = import("GPU")
local Task = import("Task")

local Config = require("./editor/config")
local Document = require("./editor/document")
local Http = require("./editor/http")
local Palette = require("./editor/palette")
local Scene = require("./editor/scene")
local Viewport = require("./editor/viewport")
local UI = require("./editor/ui")

local Vector = Primitives.Vector

-- ---------------------------------------------------------------------------
-- Estado do editor
-- ---------------------------------------------------------------------------

local editor = {
	doc = nil,
	scene = nil,
	viewport = nil,
	ui = nil,
	http = nil,
	colorIndex = Palette.defaultColorIndex,
	materialIndex = Palette.defaultMaterialIndex,
	blockSize = { 1, 1, 1 },
	step = 0.5, -- passo do arrastar (Shift = 0.25)
	grid = true,
	drag = nil, -- { origin = Vector, start = {position}, moved = false }
	lastMouse = { 0, 0 },
	valid = false,
}

-- ---------------------------------------------------------------------------
-- Helpers de bloco
-- ---------------------------------------------------------------------------

local function currentColor()
	return Palette.colors[editor.colorIndex].rgb
end

local function currentMaterial()
	return Palette.materials[editor.materialIndex].id
end

local function newBlock(position, size)
	return {
		position = { position[1], position[2], position[3] },
		size = { size[1], size[2], size[3] },
		color = { currentColor()[1], currentColor()[2], currentColor()[3] },
		material = currentMaterial(),
		collidable = true,
	}
end

--- Empilha o bloco na face do bloco atingido (ou no chão, se não houve acerto).
local function placementPosition(hitPosition, normal, size)
	local x, y, z
	if hitPosition then
		x = hitPosition.X + normal.X * size[1] * 0.5
		y = hitPosition.Y + normal.Y * size[2] * 0.5
		z = hitPosition.Z + normal.Z * size[3] * 0.5
	else
		-- Sem acerto: projeta o raio no plano y = 0 (topo da baseplate).
		local origin, forward = editor.viewport:centerRay()
		local t = 0
		if math.abs(forward.Y) > 1e-4 then
			t = (0 - origin.Y) / forward.Y
		end
		x = origin.X + forward.X * t
		z = origin.Z + forward.Z * t
		y = size[2] * 0.5
	end
	-- Encaixa no meio da célula (grade de 1 unidade).
	if editor.grid then
		x = math.floor(x) + 0.5
		z = math.floor(z) + 0.5
		-- Altura: encaixa o centro na grade apenas para blocos de altura ímpar.
		if size[2] % 2 == 1 then
			y = math.floor(y) + 0.5
		else
			y = math.floor(y + 0.5)
		end
	end
	return { x, y, z }
end

local function refreshScene()
	editor.scene:bind(editor.doc.world.blocks)
	editor.scene:rebuild(editor.doc.world.blocks)
	editor.scene:setSelection(editor.scene.selection)
end

local function refreshUi()
	local blocks = editor.doc.world.blocks
	editor.ui:setSize(editor.blockSize)
	editor.ui:setMapInfo(editor.doc.world, #blocks, editor.doc.world.spawn)
end

-- ---------------------------------------------------------------------------
-- Ações
-- ---------------------------------------------------------------------------

local function save()
	local ok, message = editor.http:saveMap(editor.doc.world)
	local path = Config.mapsDir .. "/" .. tostring(editor.doc.world.id or "baseplate") .. ".loom"
	local localOk, localMessage = editor.doc:saveLocal(path)
	if ok and localOk then
		editor.ui:setStatus("salvo no servidor e em " .. path, UI.OK)
	elseif localOk then
		editor.ui:setStatus("servidor indisponível (" .. message .. "); " .. localMessage, UI.WARN)
	else
		editor.ui:setStatus("falha ao salvar: " .. tostring(message) .. " / " .. tostring(localMessage), UI.WARN)
	end
end

local function reload()
	local world, err = editor.http:getMap(Config.world)
	if world then
		editor.doc = Document.new(world)
		editor.scene.selection = nil
		refreshScene()
		refreshUi()
		editor.ui:setStatus("mapa '" .. tostring(world.name or world.id) .. "' recarregado do servidor", UI.OK)
	else
		editor.ui:setStatus("não recarreguei (" .. tostring(err) .. "); mantendo o mapa atual", UI.WARN)
	end
end

local function pickAtCrosshair()
	local origin, forward = editor.viewport:centerRay()
	return editor.scene:pick(origin, forward, 2000)
end

local function placeBlock()
	local _index, position, normal = pickAtCrosshair()
	local target = placementPosition(position, normal, editor.blockSize)
	local newIndex = editor.doc:addBlock(newBlock(target, editor.blockSize))
	refreshScene()
	refreshUi()
	editor.ui:setStatus(
		string.format("bloco #%d em %.1f, %.1f, %.1f", newIndex, target[1], target[2], target[3]),
		UI.OK
	)
end

local function deleteBlock()
	local index = pickAtCrosshair()
	if not index then
		editor.ui:setStatus("nada para apagar", UI.MUTED)
		return
	end
	editor.doc:removeBlock(index)
	refreshScene()
	refreshUi()
	editor.ui:setStatus("bloco #" .. index .. " apagado", UI.OK)
end

local function paintBlock()
	local index = pickAtCrosshair()
	if not index then
		editor.ui:setStatus("nada para pintar", UI.MUTED)
		return
	end
	local block = editor.doc.world.blocks[index]
	block.color = { currentColor()[1], currentColor()[2], currentColor()[3] }
	block.material = currentMaterial()
	editor.doc.dirty = true
	editor.scene:movePart(index, block)
	editor.ui:setStatus("bloco #" .. index .. " pintado com " .. currentMaterial(), UI.OK)
end

local function setSpawnHere()
	local index, position = pickAtCrosshair()
	if not index or not position then
		editor.ui:setStatus("aponte para um bloco para definir o spawn", UI.MUTED)
		return
	end
	local hitBlock = editor.doc.world.blocks[index]
	editor.doc:setSpawn(hitBlock.position[1], hitBlock.position[2] + hitBlock.size[2] * 0.5 + 0.2, hitBlock.position[3])
	refreshUi()
	editor.ui:setStatus("spawn definido no bloco #" .. index, UI.OK)
end

-- ---------------------------------------------------------------------------
-- Input
-- ---------------------------------------------------------------------------

local keysDown = {}
local shift = false
local alt = false
local ctrl = false

local function isModifier(name)
	local modifiers = {
		LeftShift = true,
		RightShift = true,
		LeftAlt = true,
		RightAlt = true,
		LeftControl = true,
		RightControl = true,
	}
	return modifiers[name] == true
end

local function modifier(name, down)
	if name == "LeftShift" or name == "RightShift" then
		shift = down
	elseif name == "LeftAlt" or name == "RightAlt" then
		alt = down
	elseif name == "LeftControl" or name == "RightControl" then
		ctrl = down
	end
end

local function bindInput()
	Keyboard.InputChanged:Connect(function(_id, key, kind)
		local down = kind == "Begin"
		if isModifier(key) then
			modifier(key, down)
			return
		end
		keysDown[key] = down
		if not down then
			return
		end

		local ctrlKey = ctrl or keysDown.LeftControl or keysDown.RightControl
		if ctrlKey and key == "S" then
			save()
		elseif ctrlKey and key == "L" then
			reload()
		elseif ctrlKey and key == "Z" then
			if editor.doc:undo() then
				refreshScene()
				refreshUi()
				editor.ui:setStatus("desfeito", UI.MUTED)
			end
		elseif ctrlKey and key == "Y" then
			if editor.doc:redo() then
				refreshScene()
				refreshUi()
				editor.ui:setStatus("refeito", UI.MUTED)
			end
		elseif key == "Delete" or key == "Backspace" then
			local selected = editor.scene.selection
			if selected then
				editor.doc:removeBlock(selected)
				refreshScene()
				refreshUi()
			end
		elseif key == "G" then
			editor.grid = not editor.grid
			editor.ui:setStatus("grade " .. (editor.grid and "ligada" or "desligada"), UI.MUTED)
		elseif key == "F" then
			local selected = editor.scene.selection
			local block = selected and editor.doc.world.blocks[selected]
			if block then
				editor.viewport:focus(block.position, block.size)
			end
		elseif key == "P" then
			setSpawnHere()
		elseif key == "BracketLeft" then
			local s = editor.blockSize
			s[1] = math.max(1, s[1] - 1)
			s[3] = math.max(1, s[3] - 1)
			refreshUi()
		elseif key == "BracketRight" then
			local s = editor.blockSize
			s[1] = math.min(16, s[1] + 1)
			s[3] = math.min(16, s[3] + 1)
			refreshUi()
		elseif key == "Minus" then
			editor.blockSize[2] = math.max(1, editor.blockSize[2] - 1)
			refreshUi()
		elseif key == "Equal" then
			editor.blockSize[2] = math.min(16, editor.blockSize[2] + 1)
			refreshUi()
		end
	end)

	Mouse.InputReceived:Connect(function(button, kind)
		local down = kind == "Begin"
		if button == "MouseButton3" then
			editor.cameraDragging = down
		elseif button == "MouseButton1" and down then
			if shift then
				deleteBlock()
			elseif alt then
				paintBlock()
			else
				-- Seleciona o bloco apontado; se nada, coloca um novo.
				local index = pickAtCrosshair()
				if index then
					editor.scene:setSelection(index)
					local block = editor.doc.world.blocks[index]
					editor.drag = {
						origin = Vector.new(block.position[1], block.position[2], block.position[3]),
						index = index,
						moved = false,
					}
					editor.ui:setStatus("selecionado #" .. index, UI.ACCENT)
				else
					placeBlock()
				end
			end
		elseif button == "MouseButton1" and not down then
			editor.drag = nil
		end
	end)

	Mouse.Moved:Connect(function(pos, delta)
		editor.lastMouse = { pos.X, pos.Y }
		if editor.cameraDragging then
			editor.viewport:orbit(delta.X, delta.Y)
		elseif editor.drag then
			-- Arrasta o bloco selecionado na horizontal.
			local block = editor.doc.world.blocks[editor.drag.index]
			local step = shift and 0.25 or editor.step
			local amount = delta.X * step * 0.08
			block.position[1] = block.position[1] + amount
			block.position[3] = block.position[3] + delta.Y * step * 0.08
			editor.doc.dirty = true
			editor.drag.moved = true
			editor.scene:movePart(editor.drag.index, block)
			refreshUi()
		end
	end)

	Mouse.Scrolled:Connect(function(_dx, dy)
		editor.viewport:zoom(dy)
	end)

	-- Paleta por teclado: 1-6 escolhe o material.
	Keyboard.InputChanged:Connect(function(_id, key, kind)
		if kind ~= "Begin" then
			return
		end
		local number = tonumber(key)
		if number and number >= 1 and number <= #Palette.materials then
			editor.materialIndex = number
			editor.ui:selectMaterial(number)
		end
	end)
end

-- ---------------------------------------------------------------------------
-- Loop
-- ---------------------------------------------------------------------------

local lastFrame = 0

local function update(dt)
	-- Pan com WASD.
	local forward, right = 0, 0
	if keysDown.W or keysDown.Up then
		forward = 1
	end
	if keysDown.S or keysDown.Down then
		forward = -1
	end
	if keysDown.D or keysDown.Right then
		right = 1
	end
	if keysDown.A or keysDown.Left then
		right = -1
	end
	if forward ~= 0 or right ~= 0 then
		editor.viewport:pan(forward, right, dt)
	end

	local blocks = editor.doc.world.blocks
	local status = string.format(
		"%s   |   %d blocos   |   FPS %d   |   pos %.1f, %.1f, %.1f",
		editor.doc.dirty and "não salvo" or "salvo",
		#blocks,
		math.floor(GPU.FrameStats().FPS or 0),
		editor.viewport.target[1],
		editor.viewport.target[2],
		editor.viewport.target[3]
	)
	if editor.drag and editor.drag.moved then
		status = status .. "   |   arrastando #" .. editor.drag.index
	end
	editor.ui:setStatus(status, editor.doc.dirty and UI.WARN or UI.MUTED)
end

-- ---------------------------------------------------------------------------
-- Boot
-- ---------------------------------------------------------------------------

local win = Window.Open({ title = "Loomia Studio", width = 1280, height = 720 })
local camera = win:GetViewport()
camera.FOV = 65
camera.Near = 0.1
camera.Far = 3000
GPU.SetMaxFrameRate(120)

editor.http = Http.new(Config.server)

-- Carrega o mapa do servidor numa thread (o HTTP é bloqueante). Enquanto
-- isso, o Studio já abre com a baseplate vazia.
local loadedFromServer = false
editor.doc = Document.new(Document.empty(Config.world, "Novo mapa"))
editor.scene = Scene.new()
editor.viewport = Viewport.new(camera)

local width, height = 1280, 720
editor.ui = UI.new(width, height)
editor.ui:build(Palette, Palette.materials)
editor.ui:selectColor(editor.colorIndex)
editor.ui:selectMaterial(editor.materialIndex)

refreshScene()
refreshUi()
	local loadedText = "servidor fora do ar — modo local"
	local loadedColor = UI.WARN
	if loadedFromServer then
		loadedText = "mapa carregado do servidor"
		loadedColor = UI.OK
	end
	editor.ui:setStatus(loadedText, loadedColor)

-- Centra a câmera no spawn.
local spawn = editor.doc.world.spawn
editor.viewport.target = {
	spawn[1],
	spawn[2] + 2,
	spawn[3],
}
editor.viewport:apply()

-- Baixa o mapa do servidor em segundo plano e substitui o documento.
Task.Spawn(function()
	local world, err = editor.http:getMap(Config.world)
	if world then
		editor.doc = Document.new(world)
		loadedFromServer = true
		refreshScene()
		refreshUi()
		editor.ui:setStatus("mapa carregado do servidor", UI.OK)
		print("[Studio] mapa carregado: " .. tostring(world.name))
	else
		editor.ui:setStatus("servidor fora do ar — modo local (" .. tostring(err) .. ")", UI.WARN)
		print("[Studio] sem servidor: " .. tostring(err))
	end
end)

bindInput()

Process.Heartbeat:Connect(function(dt)
	local now = os.clock()
	local frameDt = math.min(now - lastFrame, 0.1)
	lastFrame = now
	update(frameDt)
end)
