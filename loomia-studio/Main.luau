--[[
	Loomia Studio — editor de mapas do Loomia sobre a engine Ruzit.

	FERRAMENTAS (viewport)
		LMB              seleciona o bloco apontado; sem alvo, coloca um bloco
		Shift + LMB      apaga o bloco apontado
		Alt + LMB        pinta o bloco apontado (cor + material atuais)
		Ctrl + D         duplica o selecionado
		Arrastar         move o bloco selecionado (passo 0.5; Shift = 0.25)
		RMB              orbita a câmera        MMB  move (pan)
		Roda             zoom                   F    foca no selecionado
		[ ] - =          tamanho do bloco        G    liga/desliga a grade
		P                spawn no bloco apontado

	ARQUIVO
		Ctrl+N           mapa novo               Ctrl+O  abrir mapa
		Ctrl+S           salvar (.loom + scripts)
		F5               testar o jogo (Playtest)

	ABAS (direita): INSPETOR (cor/material/tamanho/seleção), SCRIPTS (script
	do mapa) e MAPA (nome, id, spawn, céu).
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
local Catalog = require("./editor/catalog")
local Scene = require("./editor/scene")
local Viewport = require("./editor/viewport")
local UI = require("./editor/ui")
local Sky = require("./editor/sky")
local Scripts = require("./editor/scripts")
local ScriptEditor = require("./editor/script_editor")
local Playtest = require("./editor/playtest")

local Vector = Primitives.Vector

-- ---------------------------------------------------------------------------
-- Estado do editor
-- ---------------------------------------------------------------------------

local editor = {
	doc = nil,
	scripts = nil,
	scene = nil,
	viewport = nil,
	ui = nil,
	http = nil,
	scriptEditor = nil,
	playtest = nil,
	colorIndex = Palette.defaultColorIndex,
	materialIndex = Palette.defaultMaterialIndex,
	presetName = nil,
	blockSize = { 1, 1, 1 },
	step = 0.5,
	grid = true,
	drag = nil,
	lastMouse = { 0, 0 },
	filePath = nil,
	openIndex = 0,
}

local shift = false
local alt = false
local ctrl = false
local keysDown = {}

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

local function snap(position, size)
	if not editor.grid then
		return position
	end
	local x = math.floor(position[1]) + 0.5
	local z = math.floor(position[3]) + 0.5
	local y
	if size[2] % 2 == 1 then
		y = math.floor(position[2]) + 0.5
	else
		y = math.floor(position[2] + 0.5)
	end
	return { x, y, z }
end

local function placementPosition(hitPosition, normal, size)
	if hitPosition then
		return snap({
			hitPosition.X + normal.X * size[1] * 0.5,
			hitPosition.Y + normal.Y * size[2] * 0.5,
			hitPosition.Z + normal.Z * size[3] * 0.5,
		}, size)
	end
	local origin, forward = editor.viewport:centerRay()
	local t = 0
	if math.abs(forward.Y) > 1e-4 then
		t = (0 - origin.Y) / forward.Y
	end
	return snap({ origin.X + forward.X * t, size[2] * 0.5, origin.Z + forward.Z * t }, size)
end

local function refreshScene()
	editor.scene:bind(editor.doc.world.blocks)
	editor.scene:rebuild(editor.doc.world.blocks)
	editor.scene:setSelection(editor.scene.selection)
end

local function refreshUi()
	local world = editor.doc.world
	editor.ui:setSize(editor.blockSize)
	editor.ui:setMapInfo(world, #world.blocks, world.spawn, editor.filePath)
	local selected = editor.scene.selection
	local block = selected and world.blocks[selected]
	if block then
		editor.ui:setSelection(string.format(
			"#%d  %.1f, %.1f, %.1f",
			selected,
			block.position[1],
			block.position[2],
			block.position[3]
		))
	else
		editor.ui:setSelection(nil)
	end
	if editor.scripts then
		editor.ui:setScripts(editor.scripts, editor.scripts.active)
	end
end

local function refreshSky()
	Sky.apply(editor.doc.world)
end

local function reloadAll()
	editor.scene.selection = nil
	refreshScene()
	refreshUi()
	refreshSky()
	editor.viewport:focus(editor.doc.world.spawn, { 8, 8, 8 })
end

-- ---------------------------------------------------------------------------
-- Arquivos
-- ---------------------------------------------------------------------------

local function loomPath(id)
	return Config.mapsDir .. "/" .. tostring(id) .. ".loom"
end

local function save(quiet)
	local world = editor.doc.world
	local path = editor.filePath or loomPath(world.id or "baseplate")
	local localOk, localMessage = editor.doc:saveLocal(path)
	editor.filePath = path
	local scriptsOk = true
	local scriptsMessage = nil
	if editor.scripts then
		scriptsOk, scriptsMessage = editor.scripts:save(path)
	end
	local serverOk, serverMessage = editor.http:saveMap(world)
	if not quiet then
		if localOk and serverOk then
			editor.ui:setStatus("salvo: " .. path .. " (cópia no servidor)", UI.OK)
		elseif localOk then
			editor.ui:setStatus("salvo em " .. path .. "  — servidor: " .. tostring(serverMessage), UI.WARN)
		else
			editor.ui:setStatus("falha ao salvar: " .. tostring(localMessage), UI.DANGER)
		end
		if not scriptsOk then
			editor.ui:setStatus("mapa salvo, scripts falharam: " .. tostring(scriptsMessage), UI.WARN)
		end
	end
	return localOk
end

--- Abre um mapa local (`maps/<id>.loom`) e seus scripts.
local function openMap(id)
	local path = loomPath(id)
	local doc, err = Document.loadLocal(path)
	if not doc then
		editor.ui:setStatus("não abri '" .. tostring(id) .. "': " .. tostring(err), UI.DANGER)
		return false
	end
	editor.doc = doc
	editor.scripts = Scripts.load(path)
	editor.filePath = path
	reloadAll()
	editor.scriptEditor.scripts = editor.scripts
	editor.scriptEditor:load(editor.scripts.active)
	editor.ui:setScripts(editor.scripts, editor.scripts.active)
	editor.ui:setStatus("mapa aberto: " .. path, UI.OK)
	print("[Studio] mapa aberto: " .. path)
	return true
end

--- Cria um mapa novo (baseplate) e zera os scripts.
local function newMap()
	local id = "mapa" .. tostring(os.time()):sub(-6)
	editor.doc = Document.new(Document.empty(id, "Novo mapa"))
	editor.scripts = Scripts.new()
	editor.filePath = nil
	reloadAll()
	editor.scriptEditor.scripts = editor.scripts
	editor.scriptEditor:load(editor.scripts.active)
	editor.ui:setStatus("mapa novo — Ctrl+S salva em maps/" .. id .. ".loom", UI.OK)
end

local function toggleScriptEditor()
	local open = not editor.scriptEditor.open
	editor.scriptEditor.active = editor.scripts.active
	editor.scriptEditor:load(editor.scripts.active)
	editor.scriptEditor:setVisible(open)
	editor.ui:setVisible(not open)
	Mouse.Unlock()
end

-- ---------------------------------------------------------------------------
-- Playtest
-- ---------------------------------------------------------------------------

local function startPlaytest()
	if editor.playtest and editor.playtest.running then
		return
	end
	editor.playtest = Playtest.new(editor.doc, editor.scripts, {
		scene = editor.scene,
		ui = editor.ui,
		camera = editor.camera,
		width = 1280,
		height = 720,
	})
	editor.playtest:start()
	for key in pairs(keysDown) do
		keysDown[key] = false
	end
	editor.ui:setStatus("playtest em andamento — Esc volta ao editor", UI.OK)
end

local function stopPlaytest()
	if editor.playtest then
		editor.playtest:stop()
		editor.playtest = nil
		editor.ui:setStatus("playtest encerrado", UI.MUTED)
		refreshUi()
	end
end

-- ---------------------------------------------------------------------------
-- Ações de edição
-- ---------------------------------------------------------------------------

local function pickAtCrosshair()
	local origin, forward = editor.viewport:centerRay()
	return editor.scene:pick(origin, forward, 2000)
end

local function placeBlock()
	local _index, position, normal = pickAtCrosshair()
	local target = placementPosition(position, normal, editor.blockSize)
	local newIndex = editor.doc:addBlock(newBlock(target, editor.blockSize))
	refreshScene()
	editor.scene:setSelection(newIndex)
	refreshUi()
	editor.ui:setStatus(string.format("bloco #%d em %.1f, %.1f, %.1f", newIndex, target[1], target[2], target[3]), UI.OK)
end

local function placePreset(name)
	local _index, position, normal = pickAtCrosshair()
	local base = placementPosition(position, normal, { 1, 1, 1 })
	local blocks, entry = Catalog.instantiate(name, base)
	if not blocks then
		editor.ui:setStatus("preset desconhecido: " .. tostring(name), UI.DANGER)
		return
	end
	editor.doc:addBlocks(blocks)
	refreshScene()
	refreshUi()
	editor.ui:setStatus(string.format("'%s' inserido (%d blocos)", entry.name, #blocks), UI.OK)
end

local function deleteSelected()
	local index = editor.scene.selection or pickAtCrosshair()
	if not index then
		editor.ui:setStatus("nada para apagar", UI.MUTED)
		return
	end
	editor.doc:removeBlock(index)
	refreshScene()
	refreshUi()
	editor.ui:setStatus("bloco apagado", UI.OK)
end

local function paintSelected()
	local index = editor.scene.selection or pickAtCrosshair()
	if not index then
		editor.ui:setStatus("nada para pintar", UI.MUTED)
		return
	end
	local block = editor.doc.world.blocks[index]
	block.color = { currentColor()[1], currentColor()[2], currentColor()[3] }
	block.material = currentMaterial()
	editor.doc.dirty = true
	editor.scene:movePart(index, block)
	editor.ui:setStatus("bloco pintado", UI.OK)
end

local function duplicateSelected()
	local index = editor.scene.selection
	if not index then
		editor.ui:setStatus("selecione um bloco para duplicar", UI.MUTED)
		return
	end
	local newIndex = editor.doc:duplicateBlock(index, 1)
	refreshScene()
	editor.scene:setSelection(newIndex)
	refreshUi()
	editor.ui:setStatus("bloco duplicado -> #" .. tostring(newIndex), UI.OK)
end

local function setSpawnHere()
	local index = editor.scene.selection or pickAtCrosshair()
	if not index then
		editor.ui:setStatus("aponte para um bloco para definir o spawn", UI.MUTED)
		return
	end
	local block = editor.doc.world.blocks[index]
	editor.doc:setSpawn(block.position[1], block.position[2] + block.size[2] * 0.5 + 0.2, block.position[3])
	refreshUi()
	editor.ui:setStatus("spawn definido", UI.OK)
end

-- ---------------------------------------------------------------------------
-- Input
-- ---------------------------------------------------------------------------

local function isModifier(name)
	return name == "LeftShift"
		or name == "RightShift"
		or name == "LeftAlt"
		or name == "RightAlt"
		or name == "LeftControl"
		or name == "RightControl"
end

local function setModifier(name, down)
	if name == "LeftShift" or name == "RightShift" then
		shift = down
	elseif name == "LeftAlt" or name == "RightAlt" then
		alt = down
	elseif name == "LeftControl" or name == "RightControl" then
		ctrl = down
	end
end

local function handleEditorKey(key)
	local ctrlKey = ctrl or keysDown.LeftControl or keysDown.RightControl
	if ctrlKey and key == "S" then
		save(false)
	elseif ctrlKey and key == "N" then
		newMap()
	elseif ctrlKey and key == "O" then
		local maps = Document.listMaps(Config.mapsDir)
		if #maps > 0 then
			editor.openIndex = (editor.openIndex % #maps) + 1
			openMap(maps[editor.openIndex])
		else
			editor.ui:setStatus("nenhum .loom em " .. Config.mapsDir, UI.WARN)
		end
	elseif ctrlKey and key == "Z" then
		if editor.doc:undo() then
			refreshScene()
			refreshUi()
		end
	elseif ctrlKey and key == "Y" then
		if editor.doc:redo() then
			refreshScene()
			refreshUi()
		end
	elseif ctrlKey and key == "D" then
		duplicateSelected()
	elseif ctrlKey and key == "E" then
		toggleScriptEditor()
	elseif key == "F5" then
		startPlaytest()
	elseif key == "Delete" or key == "Backspace" then
		deleteSelected()
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
	elseif key == "LeftBracket" then
		editor.blockSize[1] = math.max(1, editor.blockSize[1] - 1)
		editor.blockSize[3] = editor.blockSize[1]
		refreshUi()
	elseif key == "RightBracket" then
		editor.blockSize[1] = math.min(16, editor.blockSize[1] + 1)
		editor.blockSize[3] = editor.blockSize[1]
		refreshUi()
	elseif key == "Minus" then
		editor.blockSize[2] = math.max(1, editor.blockSize[2] - 1)
		refreshUi()
	elseif key == "Equals" then
		editor.blockSize[2] = math.min(16, editor.blockSize[2] + 1)
		refreshUi()
	elseif key == "One" then
		editor.materialIndex = 1
		editor.ui:selectMaterial(1)
	elseif key == "Two" then
		editor.materialIndex = 2
		editor.ui:selectMaterial(2)
	elseif key == "Three" then
		editor.materialIndex = 3
		editor.ui:selectMaterial(3)
	elseif key == "Four" then
		editor.materialIndex = 4
		editor.ui:selectMaterial(4)
	elseif key == "Five" then
		editor.materialIndex = 5
		editor.ui:selectMaterial(5)
	elseif key == "Six" then
		editor.materialIndex = 6
		editor.ui:selectMaterial(6)
	end
end

local function bindInput()
	Keyboard.InputChanged:Connect(function(_id, key, kind)
		local down = kind == "Begin"
		if isModifier(key) then
			setModifier(key, down)
			return
		end
		keysDown[key] = down

		if editor.playtest and editor.playtest.running then
			editor.playtest:onKey(key, down)
			return
		end
		if editor.scriptEditor.open then
			editor.scriptEditor:onKey(key, down, ctrl or keysDown.LeftControl or keysDown.RightControl)
			return
		end
		if down then
			handleEditorKey(key)
		end
	end)

	Mouse.InputReceived:Connect(function(button, kind)
		local down = kind == "Begin"
		if editor.playtest and editor.playtest.running then
			editor.playtest:onMouseButton(button, down)
			return
		end
		if editor.scriptEditor.open then
			if button == "MouseButton1" and down then
				local p = Mouse.Position
				editor.scriptEditor:onClick(p.X, p.Y)
			end
			return
		end
		if button == "MouseButton3" then
			editor.cameraDragging = down
			if down then
				Mouse.Lock()
			else
				Mouse.Unlock()
			end
			return
		end
		if button ~= "MouseButton1" then
			return
		end
		if not down then
			editor.drag = nil
			return
		end

		local p = Mouse.Position
		local target = editor.ui:click(p.X, p.Y)
		if target ~= "viewport" then
			return
		end
		if shift then
			deleteSelected()
		elseif alt then
			paintSelected()
		elseif editor.presetName then
			placePreset(editor.presetName)
		else
			local index = pickAtCrosshair()
			if index then
				editor.scene:setSelection(index)
				local block = editor.doc.world.blocks[index]
				editor.drag = {
					origin = Vector.new(block.position[1], block.position[2], block.position[3]),
					index = index,
					moved = false,
				}
				refreshUi()
			else
				placeBlock()
			end
		end
	end)

	Mouse.Moved:Connect(function(_pos, delta)
		local p = Mouse.Position
		editor.lastMouse = { p.X, p.Y }
		if editor.playtest and editor.playtest.running then
			editor.playtest:onMouseMove(delta)
			return
		end
		if editor.scriptEditor.open then
			return
		end
		if editor.cameraDragging then
			editor.viewport:orbit(delta.X, delta.Y)
		elseif editor.drag then
			local block = editor.doc.world.blocks[editor.drag.index]
			local step = shift and 0.25 or editor.step
			block.position[1] = block.position[1] + delta.X * step * 0.08
			block.position[3] = block.position[3] + delta.Y * step * 0.08
			editor.doc.dirty = true
			editor.drag.moved = true
			editor.scene:movePart(editor.drag.index, block)
			refreshUi()
		end
	end)

	Mouse.Scrolled:Connect(function(_dx, dy)
		if editor.playtest and editor.playtest.running then
			editor.playtest:onScroll(dy)
			return
		end
		if editor.scriptEditor.open then
			return
		end
		editor.viewport:zoom(dy)
	end)
end

-- ---------------------------------------------------------------------------
-- UI handlers
-- ---------------------------------------------------------------------------

local function bindUi()
	editor.ui:onButton("new", newMap)
	editor.ui:onButton("open", function()
		local maps = Document.listMaps(Config.mapsDir)
		if #maps == 0 then
			editor.ui:setStatus("nenhum .loom em " .. Config.mapsDir, UI.WARN)
			return
		end
		-- ABRIR percorre a lista de `.loom` encontrados.
		editor.openIndex = (editor.openIndex % #maps) + 1
		openMap(maps[editor.openIndex])
	end)
	editor.ui:onButton("save", function()
		save(false)
	end)
	editor.ui:onButton("play", startPlaytest)
	editor.ui:onButton("deleteSel", deleteSelected)
	editor.ui:onButton("dupSel", duplicateSelected)
	editor.ui:onButton("setSpawn", setSpawnHere)
	editor.ui:onButton("scriptNew", function()
		local name = "script" .. tostring(#editor.scripts.files + 1)
		editor.scripts:set(name, Scripts.TEMPLATE)
		editor.scripts.active = name
		editor.scriptEditor.active = name
		editor.scriptEditor:load(name)
		editor.ui:setScripts(editor.scripts, name)
		toggleScriptEditor()
	end)
	editor.ui:onButton("scriptEdit", toggleScriptEditor)
	editor.ui:onButton("scriptRun", startPlaytest)

	-- Paleta: o preset escolhido fica "armado" para o próximo clique no mundo.
	for index, entry in ipairs(Catalog.list()) do
		editor.ui:onPreset(index, function()
			if editor.presetName == entry.name then
				editor.presetName = nil
				editor.ui:setInfo("bloco simples (1x1x1)")
			else
				editor.presetName = entry.name
				editor.ui:setInfo("preset: " .. entry.name)
			end
			editor.ui:selectPreset(editor.presetName)
		end)
	end

	editor.ui:onColor(function(index)
		editor.colorIndex = index
	end)
	editor.ui:onMaterial(function(index)
		editor.materialIndex = index
	end)
	editor.ui:onSky(function(rgb)
		editor.doc:setSky(rgb)
		refreshSky()
		refreshUi()
	end)
	editor.ui:onScript(function(index)
		local file = editor.scripts.files[index]
		if file then
			editor.scripts.active = file.name
			editor.scriptEditor.active = file.name
			editor.scriptEditor:load(file.name)
			editor.ui:setScripts(editor.scripts, file.name)
		end
	end)
end

-- ---------------------------------------------------------------------------
-- Loop
-- ---------------------------------------------------------------------------

local lastFrame = 0

local function updateEditor(dt)
	if editor.scriptEditor.open then
		return
	end
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

	local world = editor.doc.world
	editor.ui:setStatus(string.format(
		"%s  |  %d blocos  |  FPS %d  |  %s",
		editor.doc.dirty and "não salvo" or "salvo",
		#world.blocks,
		math.floor(GPU.FrameStats().FPS or 0),
		editor.presetName and ("preset " .. editor.presetName) or "bloco simples"
	), editor.doc.dirty and UI.WARN or UI.MUTED)
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
editor.camera = camera

editor.http = Http.new(Config.server)
editor.doc = Document.new(Document.empty(Config.world, "Novo mapa"))
editor.scripts = Scripts.new()
editor.scene = Scene.new()
editor.viewport = Viewport.new(camera)

editor.ui = UI.new(1280, 720)
editor.ui:build(Palette, Palette.materials, Catalog.list())
editor.ui:selectColor(editor.colorIndex)
editor.ui:selectMaterial(editor.materialIndex)
editor.ui:setScripts(editor.scripts, editor.scripts.active)

editor.scriptEditor = ScriptEditor.new(editor.scripts, {
	width = 1280,
	height = 720,
	onClose = function()
		editor.ui:setVisible(true)
	end,
	onSave = function()
		save(false)
		editor.scriptEditor:setStatus("salvo", UI.OK)
	end,
	isShift = function()
		return shift
	end,
})
editor.scriptEditor:build()
editor.scriptEditor:setVisible(false)

refreshScene()
refreshUi()
refreshSky()
editor.viewport:focus(editor.doc.world.spawn, { 8, 8, 8 })
editor.ui:setStatus("carregando mapa…", UI.MUTED)
print("[Studio] status ok, primitivas", #editor.ui.primitives)

-- Tenta o servidor primeiro; se não houver, abre o `.loom` local.
Task.Spawn(function()
	local world, err = editor.http:getMap(Config.world)
	if world then
		editor.doc = Document.new(world)
		editor.filePath = loomPath(world.id or Config.world)
		editor.scripts = Scripts.load(editor.filePath)
		editor.scriptEditor.scripts = editor.scripts
		editor.scriptEditor:load(editor.scripts.active)
		reloadAll()
		editor.ui:setScripts(editor.scripts, editor.scripts.active)
		editor.ui:setStatus("mapa '" .. tostring(world.name) .. "' carregado do servidor", UI.OK)
		print("[Studio] mapa carregado do servidor: " .. tostring(world.name))
		return
	end
	if not openMap(Config.world) then
		editor.ui:setStatus("servidor fora do ar (" .. tostring(err) .. "); mapa novo", UI.WARN)
	end
end)

print("[Studio] boot: bind")
bindUi()
print("[Studio] ui ok")
bindInput()
print("[Studio] input ok")

Process.Heartbeat:Connect(function(dt)
	local now = os.clock()
	local frameDt = math.min(now - lastFrame, 0.1)
	lastFrame = now
	if editor.playtest then
		editor.playtest:update(frameDt)
		if editor.playtest.requestStop then
			stopPlaytest()
		end
	else
		updateEditor(frameDt)
	end
	local _ = dt
end)
