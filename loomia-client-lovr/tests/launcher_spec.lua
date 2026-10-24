-- Verifica as áreas clicáveis do launcher (o clique precisa acertar o cartão,
-- a aba e o botão de entrar). Roda sem engine.

local here = (arg and arg[0] or ''):match('^(.*)/tests/[^/]+$') or '.'
package.path = './?.lua;./src/?.lua;' .. here .. '/?.lua;' .. here .. '/src/?.lua;' .. package.path

local function noop() end
local font = { setPixelSize = noop, getWidth = function(_, text, size) return #tostring(text) * (size or 18) * 0.55 end }

_G.lovr = {
  data = {
    encode = function(value) return require('src.json').encode(value) end,
    decode = function(_, text) return require('src.json').decode(text) end,
  },
  timer = { getTime = function() return os.clock() end },
  filesystem = { getSaveDirectory = function() return os.tmpname() .. '.dir' end },
  math = { newVec3 = function(x, y, z) return { x = x, y = y, z = z } end },
  graphics = {
    getDimensions = function() return 1280, 720 end,
    getDefaultFont = function() return font end,
    setBackgroundColor = noop,
  },
  event = { quit = noop },
  mouse = { setCursorSystem = noop },
}

local config = require('src.config')
local launcherState = require('src.states.launcher')

local app = { state = 'launcher' }
local ui = launcherState.new(app)
app.launcher = ui

config.data.token = 'sessao'
config.data.user = { nickname = 'ana', firstName = 'Ana', lastName = 'Lima', email = 'ana@loomia.dev', createdAt = '2026-10-01' }
config.data.avatars = {
  { id = '1', name = 'Aventureira', bodyColor = { 234, 190, 25 }, shirtColor = { 15, 120, 190 },
    pantsColor = { 35, 75, 45 }, hat = 'cap', isActive = true },
}
ui.maps = {
  { id = 'baseplate', name = 'Baseplate', author = 'Loomia', blocks = 10, description = 'clássico', online = 2 },
  { id = 'ilha', name = 'Ilha', author = 'ana', blocks = 40, description = 'ilha', online = 0 },
}
ui.serverStatus = { online = true }
ui.tab = 'play'

local pass = setmetatable({}, { __index = function() return noop end })
pass.setColor = noop
pass.triangle = noop
pass.text = noop
pass.setFont = noop

local passed, failed = 0, 0
local function check(name, condition, detail)
  passed = passed + 1
  if not condition then
    failed = failed + 1
    print(('  FAIL %s %s'):format(name, tostring(detail or '')))
  else
    print(('  ok   %s'):format(name))
  end
end

-- ---- aba Jogar: clicar no segundo cartão seleciona aquele mundo
ui.tab = 'play'
ui:draw(pass)
check('há áreas clicáveis na aba Jogar', #ui.ui.hits > 3, #ui.ui.hits)

-- Os cartões dos mundos têm 66 px de altura (registrados após os controles do
-- topo). Localiza pela geomometria em vez de índice fixo.
local function clickHit(hit)
  local screenY = 720 - (hit.y + hit.height - 5)
  ui:mousereleased(hit.x + 10, screenY, 1)
end
local cards = {}
for _, hit in ipairs(ui.ui.hits) do
  if hit.height == 66 then cards[#cards + 1] = hit end
end
check('dois cartões de mundo na tela', #cards == 2, #cards)
clickHit(cards[2])
check('clicar no cartão seleciona o mundo', ui.selectedMap == 'ilha', ui.selectedMap)

-- ---- botão "ENTRAR NO MUNDO" chama o fluxo de entrada
local entered = nil
ui.enterMap = function(self, map) entered = map.id end
ui.tab = 'play'
ui:draw(pass)
local enterHit
for _, hit in ipairs(ui.ui.hits) do
  if hit.height == 44 then enterHit = hit end
end
check('botão de entrar registrado', enterHit ~= nil)
if enterHit then
  local screenY = 720 - (enterHit.y + enterHit.height / 2)
  ui:mousereleased(enterHit.x + 10, screenY, 1)
  ui.update(ui, 1 / 60) -- executa o trabalho enfileirado
  check('entrar no mundo dispara a ação', entered ~= nil, entered)
end

-- ---- abas trocam de tela
ui.tab = 'profile'
ui:draw(pass)
check('aba perfil desenha', #ui.ui.hits > 0)
ui.tab = 'avatar'
ui:startEditor(nil)
ui:draw(pass)
check('editor desenha com áreas clicáveis', #ui.ui.hits >= 8, #ui.ui.hits)

print(('\n%d passaram, %d falharam'):format(passed, failed))
os.exit(failed == 0 and 0 or 1)
