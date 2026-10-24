-- Ponto de entrada do cliente Loomia (engine Lua).
--
-- Fluxo:
--   1. lê os argumentos (server=..., token=...) — vindos do link loomia://
--   2. abre SEMPRE o launcher (login, perfil, avatares, catálogo)
--   3. se houver link, inicia direto o carregamento daquele mapa
--   4. no jogo: input -> previsão -> UDP -> reconciliação -> render
--
-- A UI não depende do servidor: o launcher abre mesmo com o master fora do
-- ar (catálogo em cache + aviso de "offline").

local args = require('src.args')
local hud = require('src.hud')
local net = require('src.net')
local physics = require('src.physics')
local world = require('src.world')
local avatar = require('src.avatar')
local camera = require('src.camera')
local ui = require('src.ui')
local config = require('src.config')
local launcherState = require('src.states.launcher')
local loading = require('src.states.loading')
local game = require('src.states.game')

local app = {
  state = 'launcher',
  args = args,
  time = 0,
}

app.modules = {
  hud = hud,
  net = net,
  physics = physics,
  world = world,
  avatar = avatar,
  camera = camera,
  ui = ui,
}

--- Troca de estado num lugar só (nunca "engole" a transição).
function app.go(state)
  local previous = app.state
  app.state = state
  local current = app[state]
  if current and current.enter and previous ~= state then
    pcall(current.enter, current)
  end
end

function lovr.load()
  lovr.graphics.setBackgroundColor(0.055, 0.082, 0.137)
  hud.load()

  -- Origem do master: do link (prioridade) ou a salva no disco.
  local origin = (args and args.origin) or nil
  config.load(origin or 'http://127.0.0.1:8080')
  if origin and origin ~= '' then
    config.data.origin = origin:gsub('/+$', '')
  end

  app.launcher = launcherState.new(app)
  app.loading = loading.new(app)
  app.game = game.new(app)
  app.ui = app.launcher.ui

  -- Com link do site, vai direto para o mapa; sem link, launcher.
  if app.args.valid then
    app.loading:start(app.args)
    app.state = 'loading'
  else
    app.state = 'launcher'
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
    local transfer = current.transfer
    if nextState == 'game' then
      if transfer then
        -- Se `start` falhar, a falha aparece na tela de carregamento (antes o
        -- erro era engolido e o cliente ficava preso para sempre).
        local ok, errorMessage = pcall(app.game.start, app.game, transfer)
        if ok then
          app.state = 'game'
        else
          app.state = 'loading'
          if app.loading and app.loading.fail then
            app.loading:fail('não foi possível iniciar o jogo: ' .. tostring(errorMessage))
          end
        end
      end
    else
      app.state = nextState
      local target = app[nextState]
      if target and target.enter then pcall(target.enter, target) end
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
  -- O launcher age no clique (soltar): evita disparo duplo.
  if current and current.mousereleased then current.mousereleased(current, x, y, button) end
end

function lovr.wheelmoved(dx, dy)
  local current = app[app.state]
  if current and current.wheelmoved then current.wheelmoved(current, dx, dy) end
end

function lovr.quit()
  if app.game and app.game.quit then app.game.quit() end
end

-- Exposto para testes e depuração (`dofile('main.lua')`).
return { app = app, load = lovr.load, update = lovr.update, draw = lovr.draw,
  keypressed = lovr.keypressed, keyreleased = lovr.keyreleased, textinput = lovr.textinput,
  mousemoved = lovr.mousemoved, mousepressed = lovr.mousepressed,
  mousereleased = lovr.mousereleased, wheelmoved = lovr.wheelmoved }
