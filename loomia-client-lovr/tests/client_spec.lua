-- Teste de fumaça dos estados do cliente (sem engine, com stubs).
--
--   lua loomia-client-lovr/tests/client_spec.lua
--
-- Garante as regressões que causaram os bugs relatados:
--   1. a transição loading -> game acontece (o `nextState` não é engolido);
--   2. o loading NÃO trava quando o servidor está inacessível (mostra erro);
--   3. o launcher abre e desenha mesmo sem servidor.

local here = (arg and arg[0] or ''):match('^(.*)/tests/[^/]+$') or '.'
package.path = './?.lua;./src/?.lua;' .. here .. '/?.lua;' .. here .. '/src/?.lua;' .. package.path

-- ---------------------------------------------------------------------------
-- Stubs do engine
-- ---------------------------------------------------------------------------

local function noop() end
local calls = { drawn = 0, cleared = 0 }

local font = {
  setPixelSize = noop,
  getWidth = function(_, text, size) return #tostring(text) * 8 end,
  getHeight = function() return 18 end,
}
_G.lovr = {
  data = {
    encode = function(value)
      return require('src.json').encode(value)
    end,
    decode = function(_, text)
      return require('src.json').decode(text)
    end,
  },
  timer = { getTime = function() return os.clock() end, getDelta = function() return 1 / 60 end },
  filesystem = {
    getSaveDirectory = function() return os.tmpname():gsub('[/\\][^/\\]*$', '') end,
    getSource = function() return here end,
  },
  math = {
    newVec3 = function(x, y, z) return { x = x, y = y, z = z } end,
    newMat4 = function() return {} end,
  },
  graphics = {
    getDimensions = function() return 1280, 720 end,
    getDefaultFont = function() return font end,
    setBackgroundColor = noop,
    newModel = function()
      return {
        addMesh = function()
          return { setPosition = noop, setMaterial = noop, setEmissive = noop }
        end,
        setPosition = noop,
        setOrientation = noop,
        setParent = noop,
        setEmissive = noop,
      }
    end,
    newBox = function() return {} end,
    newMaterial = function() return {} end,
    newText = function() return {} end,
    setCamera = noop,
    newImage = function() return {} end,
  },
  event = { quit = function() calls.quit = true end },
}

local pass = {
  setColor = noop,
  clear = function() calls.cleared = calls.cleared + 1 end,
  setFont = noop,
  text = noop,
  print = noop,
  triangle = noop,
  plane = noop,
  draw = noop,
  setCamera = noop,
}

-- ---------------------------------------------------------------------------
-- Runner
-- ---------------------------------------------------------------------------

local passed, failed = 0, 0
local function check(name, condition, detail)
  if condition then
    passed = passed + 1
    print(('  ok   %s'):format(name))
  else
    failed = failed + 1
    print(('  FAIL %s %s'):format(name, tostring(detail or '')))
  end
end

-- ---------------------------------------------------------------------------
-- 1. main.lua: transição loading -> game
-- ---------------------------------------------------------------------------

print('fluxo de estados')
local main = dofile(here .. '/main.lua')
main = { app = main.app, load = main.load, update = main.update, draw = main.draw,
  keypressed = main.keypressed }

-- Carrega o app sem link (vai para o launcher).
main.load()
check('sem link abre o launcher', main.app and main.app.state == 'launcher')

-- Simula o loading terminando com um transfer pronto.
local world = {
  draw = noop, blocks = {}, minY = -40, blockCount = 1, spawn = { 0, 1, 0 }, name = 'teste',
}
local net = {
  onMessage = {},
  poll = noop,
  ping = noop,
  send = noop,
  respawn = noop,
  close = noop,
  pingMs = 5,
  ack = 1,
}
main.app.state = 'loading'
main.app.loading.stage = 6
main.app.loading.transfer = {
  world = world,
  net = net,
  welcome = { id = 7, spawn = { 0, 1, 0 }, players = {}, chat = {} },
  args = { name = 'Teste', server = '127.0.0.1:7800' },
  appearance = { username = 'Teste', body_color = { 1, 2, 3 }, shirt_color = { 1, 2, 3 }, pants_color = { 1, 2, 3 } },
}
main.app.loading.nextState = 'game'
main.update(1 / 60)
check('loading -> game troca de estado', main.app.state == 'game', main.app.state)
check('o jogo iniciou (localId lido do welcome)', main.app.game and main.app.game.localId == 7)

if main.app.state == 'game' then
  -- O jogo desenha sem erro e o Esc volta ao launcher.
  local ok, drawError = pcall(main.draw, pass)
  check('desenho do jogo não quebra', ok, drawError)
  pcall(function() main.app.game:leave() end)
  check('Esc/leave volta ao launcher', main.app.state == 'launcher', main.app.state)
end

-- ---------------------------------------------------------------------------
-- 2. loading: falha guardada, não trava
-- ---------------------------------------------------------------------------

print('loading com servidor fora do ar')
local loading = require('src.states.loading')
local appForLoading = { state = 'loading' }
local state = loading.new(appForLoading)
state:start({
  server = '127.0.0.1:9', host = '127.0.0.1', port = 9, token = 'x', map = 'baseplate',
  origin = 'http://127.0.0.1:9', name = 'Teste',
})
appForLoading.loading = state

local errors = 0
for _ = 1, 12 do
  local ok = pcall(state.update, state, 1 / 60)
  if not ok then errors = errors + 1 end
end
check('update com servidor offline não lança exceção', errors == 0, errors)
check('falha é registrada (a tela mostra o motivo)', state.failed ~= nil, state.failed)
local okDraw, drawError = pcall(state.draw, state, pass)
check('tela de erro desenha', okDraw, drawError)

-- ---------------------------------------------------------------------------
-- 3. launcher: abre e desenha offline
-- ---------------------------------------------------------------------------

print('launcher offline')
local launcher = require('src.states.launcher')
local appForLauncher = { state = 'launcher' }
local ui = launcher.new(appForLauncher)
appForLauncher.launcher = ui
appForLauncher.loading = { start = noop }

ui.checkServer = function() return false end -- força offline
ui.lastHealthAt = -1000
local pumped = 0
for _ = 1, 5 do
  local ok = pcall(ui.update, ui, 1 / 60)
  pumped = pumped + (ok and 1 or 0)
end
check('update offline não quebra', pumped == 5, pumped)
local okLauncher, launcherError = pcall(ui.draw, ui, pass)
check('a interface desenha sem servidor', okLauncher, launcherError)

local config = require('src.config')
config.data.token = nil
config.data.user = nil
local okLogin, loginError = pcall(ui.draw, ui, pass)
check('sem login mostra a tela de conta', okLogin, loginError)

print(('\n%d passaram, %d falharam'):format(passed, failed))
os.exit(failed == 0 and 0 or 1)
