-- HUD 2D: lista de jogadores, chat e diagnóstico (F1).
--
-- Tudo é desenhado em espaço de tela: o `pass` do draw aceita coordenadas em
-- pixels com a origem no canto INFERIOR esquerdo (y cresce para cima).

local hud = {}
hud.__index = hud

local function clamp(value, lo, hi)
  if value < lo then return lo end
  if value > hi then return hi end
  return value
end

function hud.new()
  local self = setmetatable({}, hud)
  self.font = lovr.graphics.getDefaultFont()
  self.chat = {}          -- { from, text, age }
  self.inputOpen = false
  self.inputText = ''
  self.inputHint = ''
  self.playerNames = {}
  self.showDebug = false
  self.stats = ''
  self.maxChat = 8
  self.chatLifetime = 25
  return self
end

function hud:load()
  if not self.font then self.font = lovr.graphics.getDefaultFont() end
end

function hud:addChat(from, text)
  self.chat[#self.chat + 1] = { from = from, text = text, age = 0 }
  while #self.chat > self.maxChat do table.remove(self.chat, 1) end
end

function hud:setPlayers(names)
  self.playerNames = names
end

function hud:update(dt)
  for index = #self.chat, 1, -1 do
    local line = self.chat[index]
    line.age = line.age + dt
    if line.age > self.chatLifetime then table.remove(self.chat, index) end
  end
end

function hud:setChatOpen(open)
  self.inputOpen = open
  if not open then
    self.inputText = ''
    self.inputHint = ''
  end
end

function hud:textinput(text)
  if not self.inputOpen then return end
  if #self.inputText < 120 then
    self.inputText = self.inputText .. text
  end
end

function hud:keypressed(key)
  if not self.inputOpen then return nil end
  if key == 'backspace' then
    self.inputText = self.inputText:sub(1, -2)
    return nil
  elseif key == 'escape' then
    self:setChatOpen(false)
    return 'cancel'
  elseif key == 'return' then
    local text = self.inputText
    self:setChatOpen(false)
    return text
  end
  return nil
end

function hud:drawPanel(pass, x, y, width, height)
  -- Triângulo deitado no plano XY da tela (origin + 2 arestas).
  pass:setColor(0, 0, 0, 0.55)
  pass:triangle(
    x, y, 0,
    x + width, y, 0,
    x, y + height, 0
  )
  pass:triangle(
    x + width, y, 0,
    x + width, y + height, 0,
    x, y + height, 0
  )
end

--- Desenha HUD + chat. `info` traz fps, ping, jogadores etc.
function hud:draw(pass, info)
  local width, height = lovr.graphics.getDimensions()
  local scale = math.max(0.5, height / 900)
  local function print(text, x, y, r, g, b, a)
    self.font:setPixelSize(18 * scale)
    pass:setFont(self.font)
    pass:setColor(r or 1, g or 1, b or 1, a or 1)
    pass:print(text, x, y, 0.5, 0, math.pi)
  end

  -- Painel de jogadores (canto superior esquerdo).
  self:drawPanel(pass, 12, 12, 230 * scale, (28 + #self.playerNames * 18) * scale)
  print('LOOMIA', 22, height - 34 * scale, 0.45, 0.85, 1, 1)
  print(('%d online'):format(#self.playerNames), 22, height - 56 * scale, 0.75, 0.78, 0.85, 1)
  for index, name in ipairs(self.playerNames) do
    print(name, 22, height - (80 + index * 18) * scale, 1, 1, 1, 1)
  end

  -- Chat (canto inferior esquerdo, acima do campo de digitação).
  local chatY = height - (60 + (self.inputOpen and 34 or 0) * scale)
  for index = #self.chat, 1, -1 do
    local line = self.chat[index]
    local alpha = clamp((self.chatLifetime - line.age) / 5, 0, 1)
    chatY = chatY + 20 * scale
    print(('[%s] %s'):format(line.from, line.text), 22, chatY, 1, 1, 1, alpha)
  end

  if self.inputOpen then
    local boxY = height - 46 * scale
    self:drawPanel(pass, 12, boxY - 8, (width - 24), 34 * scale)
    local caret = (math.floor(lovr.timer.getTime() * 2) % 2 == 0) and '|' or ' '
    print('Dizendo: ' .. self.inputText .. caret, 22, boxY, 0.55, 0.85, 1, 1)
    if self.inputHint ~= '' then
      print(self.inputHint, 22, boxY - 22 * scale, 0.6, 0.65, 0.72, 1)
    end
  end

  -- Diagnóstico (F1): FPS, ping, tick recebido, jogadores.
  if self.showDebug then
    local lines = info or {}
    self:drawPanel(pass, width - 330 * scale, height - (18 + #lines * 18) * scale, 318 * scale, (#lines * 18 + 12) * scale)
    for index, line in ipairs(lines) do
      print(line, width - 318 * scale, height - (18 + index * 18) * scale, 0.75, 0.9, 1, 1)
    end
  end
end

return hud
