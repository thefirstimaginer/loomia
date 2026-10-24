-- Interface 2D do cliente (launcher): desenho em tela + widgets simples.
--
-- A UI NÃO depende do servidor: ela abre sempre, mostra "offline" e o botão
-- "Conectar" quando o master não responde. Só login, catálogo e mapa dependem
-- da rede.

local ui = {}
ui.__index = ui

ui.COLORS = {
  bg = { 0.055, 0.082, 0.137 },
  panel = { 0.086, 0.117, 0.188 },
  panelSoft = { 0.11, 0.15, 0.24 },
  line = { 0.18, 0.24, 0.35 },
  text = { 0.91, 0.945, 0.984 },
  muted = { 0.56, 0.63, 0.74 },
  accent = { 0.29, 0.66, 1.0 },
  ok = { 0.24, 0.86, 0.59 },
  warn = { 0.94, 0.63, 0.08 },
  danger = { 1.0, 0.36, 0.36 },
  gold = { 0.918, 0.745, 0.098 },
}

function ui.new()
  local self = setmetatable({}, ui)
  self.font = lovr.graphics.getDefaultFont()
  self.hits = {} -- botões desenhados no frame (para o clique)
  self.hover = nil
  self.scroll = 0
  return self
end

function ui:size()
  return lovr.graphics.getDimensions()
end

function ui:color(name, alpha)
  local c = ui.COLORS[name] or ui.COLORS.text
  return c[1], c[2], c[3], alpha or 1
end

--- Retângulo preenchido.
function ui:rect(pass, x, y, width, height, color, alpha)
  pass:setColor(self:color(color, alpha))
  pass:triangle(x, y, 0, x + width, y, 0, x, y + height, 0)
  pass:triangle(x + width, y, 0, x + width, y + height, 0, x, y + height, 0)
end

--- Retângulo com borda (estilo cartão).
function ui:card(pass, x, y, width, height, color, alpha)
  self:rect(pass, x, y, width, height, color or 'panel', alpha or 1)
  self:rect(pass, x, y, width, 1, 'line', 0.9)
end

--- Texto simples (origem no canto inferior esquerdo da tela).
function ui:text(pass, value, x, y, size, color, alpha, center)
  self.font:setPixelSize(size or 18)
  pass:setFont(self.font)
  pass:setColor(self:color(color or 'text', alpha))
  local width = self.font:getWidth(value, size or 18)
  if center then x = x - width / 2 end
  pass:text(value, x, y, 0, 0, math.pi)
  return width
end

--- Botão. `onClick` é chamado quando o usuário solta o mouse dentro.
function ui:button(pass, options)
  local x, y, width, height = options.x, options.y, options.width, options.height
  local hovered = false
  if self.hover then
    hovered = self.hover.x == x and self.hover.y == y and self.hover.width == width
  end
  local fill = options.variant or 'panelSoft'
  if options.disabled then
    fill = 'panel'
  elseif hovered then
    fill = options.variant == 'accent' and 'accent' or 'line'
  end
  if options.variant == 'accent' and not options.disabled then
    self:rect(pass, x, y, width, height, 'accent', hovered and 1 or 0.92)
    self:text(pass, options.label, x + width / 2, y + height / 2 - (options.size or 15) / 2,
      options.size or 15, 'bg', 1, true)
  else
    self:rect(pass, x, y, width, height, fill)
    self:rect(pass, x, y, width, 1, 'line', 0.8)
    local color = options.disabled and 'muted' or (options.color or 'text')
    self:text(pass, options.label, x + width / 2, y + height / 2 - (options.size or 15) / 2,
      options.size or 15, color, 1, true)
  end
  if not options.disabled then
    self.hits[#self.hits + 1] = {
      x = x, y = y, width = width, height = height,
      onClick = options.onClick, cursor = options.cursor,
    }
  end
end

--- Campo de texto (mostra o valor ou o placeholder).
function ui:field(pass, options)
  local x, y, width, height = options.x, options.y, options.width, options.height
  local active = options.active
  self:rect(pass, x, y, width, height, 'panelSoft')
  self:rect(pass, x, y, width, 1, active and 'accent' or 'line', 1)
  if active then
    self:rect(pass, x, y, width, height, 'accent', 0.08)
  end
  local value = options.value or ''
  local display = value
  if active and (math.floor(lovr.timer.getTime() * 2) % 2 == 0) then
    display = value .. '|'
  end
  if display == '' then
    display = options.placeholder or ''
  end
  self:text(pass, display, x + 12, y + height / 2 - 8, 15, value == '' and 'muted' or 'text')
  if options.onClick then
    self.hits[#self.hits + 1] = { x = x, y = y, width = width, height = height, onClick = options.onClick }
  end
end

--- Seletor horizontal (chapéus).
function ui:chips(pass, options)
  local x, y = options.x, options.y
  for _, choice in ipairs(options.items) do
    local width = 92
    local selected = options.value == choice.id
    self:rect(pass, x, y, width, 30, selected and 'gold' or 'panelSoft', selected and 1 or 0.9)
    self:text(pass, choice.label, x + width / 2, y + 8, 13, selected and 'bg' or 'muted', 1, true)
    local choiceId = choice.id
    self.hits[#self.hits + 1] = {
      x = x, y = y, width = width, height = 30,
      onClick = function() options.onChange(choiceId) end,
    }
    x = x + width + 8
  end
end

--- Amostras de cor.
function ui:swatches(pass, options)
  local x, y = options.x, options.y
  for _, hex in ipairs(options.colors) do
    local r = tonumber(hex:sub(2, 3), 16) / 255
    local g = tonumber(hex:sub(4, 5), 16) / 255
    local b = tonumber(hex:sub(6, 7), 16) / 255
    pass:setColor(r, g, b, 1)
    pass:triangle(x, y, 0, x + 26, y, 0, x, y + 26, 0)
    pass:triangle(x + 26, y, 0, x + 26, y + 26, 0, x, y + 26, 0)
    local selected = options.value == hex
    if selected then
      self:rect(pass, x - 2, y - 2, 30, 2, 'text')
      self:rect(pass, x - 2, y + 26, 30, 2, 'text')
      self:rect(pass, x - 2, y - 2, 2, 30, 'text')
      self:rect(pass, x + 28, y - 2, 2, 30, 'text')
    end
    local colorHex = hex
    self.hits[#self.hits + 1] = {
      x = x - 2, y = y - 2, width = 30, height = 30,
      onClick = function() options.onChange(colorHex) end,
    }
    x = x + 34
  end
end

function ui:beginFrame()
  self.hits = {}
end

--- Converte coordenadas do mouse (origem no topo) para a nossa (origem no rodapé).
function ui:toScreenY(y)
  local _, height = self:size()
  return height - y
end

function ui:pointer(x, y)
  local sy = self:toScreenY(y)
  self.hover = nil
  for _, hit in ipairs(self.hits) do
    if x >= hit.x and x <= hit.x + hit.width and sy >= hit.y and sy <= hit.y + hit.height then
      self.hover = hit
      return hit
    end
  end
  return nil
end

function ui:click(x, y)
  local hit = self:pointer(x, y)
  if hit and hit.onClick then
    hit.onClick()
    return true
  end
  return false
end

function ui:isOver(x, y)
  return self:pointer(x, y) ~= nil
end

return ui
