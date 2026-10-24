-- Gera PNGs do launcher (todas as abas) sem abrir o engine.
--
--   lua loomia-client-lovr/tests/render_launcher.lua /tmp/saida
--
-- Os arquivos saem em PPM (RGB) convertidos para PNG via ImageMagick/python se
-- disponível. Serve para revisar o layout de forma objetiva.

local outDir = (arg and arg[1]) or '/tmp'
local here = (arg and arg[0] or ''):match('^(.*)/tests/[^/]+$') or '.'
package.path = './?.lua;./src/?.lua;' .. here .. '/?.lua;' .. here .. '/src/?.lua;' .. package.path

local WIDTH, HEIGHT = 1280, 720
local pixels = {}
for index = 0, WIDTH * HEIGHT - 1 do
  pixels[index] = { 0.055, 0.082, 0.137 }
end

local function setPixel(x, y, r, g, b, a)
  -- y da UI: origem no rodapé. Armazena com origem no topo (padrão PPM).
  local px = math.floor(x) + 1
  local py = HEIGHT - math.floor(y)
  if px < 1 or px > WIDTH or py < 1 or py > HEIGHT then return end
  local current = pixels[(py - 1) * WIDTH + (px - 1)]
  a = a or 1
  current[1] = current[1] * (1 - a) + r * a
  current[2] = current[2] * (1 - a) + g * a
  current[3] = current[3] * (1 - a) + b * a
end

local pass = {}
function pass:setColor(r, g, b, a)
  self.r, self.g, self.b, self.a = r, g, b, a
end
function pass:triangle(x1, y1, _, x2, y2, _, x3, y3)
  local minX = math.min(x1, x2, x3)
  local maxX = math.max(x1, x2, x3)
  local minY = math.min(y1, y2, y3)
  local maxY = math.max(y1, y2, y3)
  local function edge(ax, ay, bx, by, px, py)
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax)
  end
  for y = math.floor(minY), math.ceil(maxY) do
    for x = math.floor(minX), math.ceil(maxX) do
      local e1 = edge(x1, y1, x2, y2, x, y)
      local e2 = edge(x2, y2, x3, y3, x, y)
      local e3 = edge(x3, y3, x1, y1, x, y)
      if (e1 >= 0 and e2 >= 0 and e3 >= 0) or (e1 <= 0 and e2 <= 0 and e3 <= 0) then
        setPixel(x, y, self.r, self.g, self.b, self.a)
      end
    end
  end
end

local function font()
  return {
    setPixelSize = function(self, size) self.size = size end,
    getWidth = function(_, text, size) return #tostring(text) * (size or 16) * 0.55 end,
  }
end
local sharedFont = font()

function pass:setFont(value) self.font = value end
function pass:text(text, x, y)
  local size = (self.font and self.font.size) or 16
  local advance = size * 0.55
  -- "Texto" desenhado como barras (não há fonte real aqui): mostra a mancha.
  for index = 1, #tostring(text) do
    local char = tostring(text):sub(index, index)
    if char then
      local cx = x + (index - 1) * advance
      local height = size * 0.72
      self:triangle(cx, y, 0, cx + advance * 0.8, y, 0, cx, y + height, 0)
      self:triangle(cx + advance * 0.8, y, 0, cx + advance * 0.8, y + height, 0, cx, y + height, 0)
    end
  end
end
function pass:print(text, x, y) self:text(text, x, y) end
function pass:clear() end
function pass:plane() end
function pass:draw() end
function pass:setCamera() end

local function noop() end
_G.lovr = {
  data = {
    encode = function(value) return require('src.json').encode(value) end,
    decode = function(_, text) return require('src.json').decode(text) end,
  },
  timer = { getTime = function() return os.clock() end, getDelta = function() return 1 / 60 end },
  filesystem = { getSaveDirectory = function() return outDir end },
  math = { newVec3 = function(x, y, z) return { x = x, y = y, z = z } end },
  graphics = {
    getDimensions = function() return WIDTH, HEIGHT end,
    getDefaultFont = function() return sharedFont end,
    setBackgroundColor = noop,
    newModel = function()
      return { addMesh = function() return { setPosition = noop } end, setPosition = noop,
        setOrientation = noop, setParent = noop }
    end,
    newBox = function() return {} end,
    newMaterial = function() return {} end,
    newText = function() return {} end,
    newImage = function() return {} end,
    setCamera = noop,
  },
  event = { quit = noop },
  mouse = { setCursorSystem = noop },
}

local config = require('src.config')
local launcherState = require('src.states.launcher')
local loadingState = require('src.states.loading')

local function save(name)
  local file = io.open(('%s/%s.ppm'):format(outDir, name), 'wb')
  if not file then return end
  file:write(('P6\n%d %d\n255\n'):format(WIDTH, HEIGHT))
  for index = 0, WIDTH * HEIGHT - 1 do
    local color = pixels[index]
    file:write(string.char(math.floor(color[1] * 255), math.floor(color[2] * 255), math.floor(color[3] * 255)))
  end
  file:close()
  for index = 0, WIDTH * HEIGHT - 1 do
    pixels[index] = { 0.055, 0.082, 0.137 }
  end
  print('  salvo:', name .. '.ppm')
end

local app = { state = 'launcher' }
app.launcher = launcherState.new(app)
app.launcher.checkServer = function() return false end
app.launcher.lastHealthAt = -1000
app.launcher.serverStatus = { online = false }

config.data.token = nil
config.data.user = nil
config.data.avatars = {}
app.launcher.tab = 'account'
app.launcher:draw(pass)
save('launcher-login')

config.data.token = 'sessao'
http = require('src.http')
http.token = 'sessao'
config.data.user = {
  nickname = 'ana_lima', firstName = 'Ana', lastName = 'Lima',
  email = 'ana@loomia.dev', createdAt = '2026-10-01T12:00:00Z',
}
config.data.avatars = {
  { id = '1', name = 'Aventureira', bodyColor = { 234, 190, 25 }, shirtColor = { 15, 120, 190 },
    pantsColor = { 35, 75, 45 }, hat = 'cap', isActive = true },
  { id = '2', name = 'Pirata', bodyColor = { 224, 175, 140 }, shirtColor = { 190, 40, 40 },
    pantsColor = { 30, 30, 40 }, hat = 'top_hat', isActive = false },
  { id = '3', name = 'Coroada', bodyColor = { 245, 210, 170 }, shirtColor = { 110, 60, 160 },
    pantsColor = { 60, 60, 120 }, hat = 'crown', isActive = false },
}

app.launcher.tab = 'profile'
app.launcher:draw(pass)
save('launcher-profile')

app.launcher:startEditor(nil)
app.launcher:draw(pass)
save('launcher-editor')

app.launcher.maps = {
  { id = 'baseplate', name = 'Baseplate Clássica', author = 'Loomia', blocks = 128, description = 'O mundo clássico de 2008, com torres para escalar.', online = 3 },
  { id = 'ilha-perdida', name = 'Ilha Perdida', author = 'ana_lima', blocks = 402, description = 'Explore a ilha e monte sua base.', online = 0 },
  { id = 'corrida', name = 'Corrida de Blocos', author = 'bruno', blocks = 96, description = 'Pista em zigue-zague.', online = 1 },
}
app.launcher.selectedMap = 'baseplate'
app.launcher.tab = 'play'
app.launcher:draw(pass)
save('launcher-play')

local loading = loadingState.new(app)
loading:start({ origin = 'http://127.0.0.1:9' })
loading.failed = 'a sala não respondeu (tempo esgotado)'
loading.detail = 'Validando o acesso…'
loading.progress = 0.9
loading:draw(pass)
save('loading-error')

print('pronto. Converta com: python3 -c "from PIL import Image; ..." ou convert')
