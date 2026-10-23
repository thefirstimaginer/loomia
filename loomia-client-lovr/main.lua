-- Ponto de entrada do LOVR para o cliente Loomia.
--
-- Fluxo:
--   1. lê os argumentos (server=..., token=...), vindos do link loomia://
--   2. se houver servidor, carrega o mapa do master e conecta na sala (Go)
--   3. senão, abre o menu para digitar host:porta + token
--   4. no jogo: input -> previsão -> UDP -> reconcialiação -> render

local args = require('src.args')
local hud = require('src.hud')
local net = require('src.net')
local physics = require('src.physics')
local world = require('src.world')
local avatar = require('src.avatar')
local camera = require('src.camera')
local loading = require('src.states.loading')
local menu = require('src.states.menu')
local game = require('src.states.game')

local app = {
  state = 'loading',
  args = args,
  time = 0,
}

-- Guarda os módulos para os estados acessarem sem `require` repetido.
app.modules = {
  hud = hud,
  net = net,
  physics = physics,
  world = world,
  avatar = avatar,
  camera = camera,
}

function lovr.load()
  lovr.graphics.setBackgroundColor(0.55, 0.75, 0.94)
  hud.load()
  app.loading = loading.new(app)
  app.menu = menu.new(app)
  app.game = game.new(app)
  -- Com link do site, vai direto para a sala; sem link, abre o menu.
  if app.args.valid then
    app.loading.start(app.args)
    app.state = 'loading'
  else
    app.state = 'menu'
  end
end

function lovr.update(dt)
  app.time = app.time + dt
  local current = app[app.state]
  if not current then return end
  if current.update then current.update(current, dt) end
  -- Transições pedidas pelo estado atual.
  if current.nextState then
    local nextState = current.nextState
    current.nextState = nil
    app.state = nextState
    if app.state == 'game' then
      app.game.start(current.transfer)
    end
  end
end

function lovr.draw(pass)
  local current = app[app.state]
  if not current then return end
  if current.draw then current.draw(current, pass) end
end

function lovr.keypressed(key)
  local current = app[app.state]
  if current and current.keypressed then current.keypressed(current, key) end
end

function lovr.keyreleased(key)
  local current = app[app.state]
  if current and current.keyreleased then current.keyreleased(current, key) end
end

function lovr.textinput(text)
  local current = app[app.state]
  if current and current.textinput then current.textinput(current, text) end
end

function lovr.mousemoved(x, y, dx, dy)
  local current = app[app.state]
  if current and current.mousemoved then current.mousemoved(current, x, y, dx, dy) end
end

function lovr.mousepressed(x, y, button)
  local current = app[app.state]
  if current and current.mousepressed then current.mousepressed(current, x, y, button) end
end

function lovr.mousereleased(x, y, button)
  local current = app[app.state]
  if current and current.mousereleased then current.mousereleased(current, x, y, button) end
end

function lovr.wheelmoved(dx, dy)
  local current = app[app.state]
  if current and current.wheelmoved then current.wheelmoved(current, dx, dy) end
end

function lovr.quit()
  if app.game and app.game.quit then app.game.quit() end
end
