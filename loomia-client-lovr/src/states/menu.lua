-- Menu: usado quando o cliente é aberto sem o link `loomia://`.
--
-- O jogador digita o endereço da sala (host:porta) e o token; o mesmo fluxo de
-- carregamento do link é executado.

local menu = {}
menu.__index = menu

local DEFAULT_SERVER = '127.0.0.1:7801'

function menu.new(app)
  local self = setmetatable({}, menu)
  self.app = app
  self.server = DEFAULT_SERVER
  self.token = ''
  self.name = (app.args and app.args.name) or 'Loomian'
  self.origin = (app.args and app.args.origin) or 'http://127.0.0.1:8080'
  self.field = 'server' -- campo em edição
  self.message = nil
  self.caret = 0
  return self
end

local function submit(self)
  local host, port = self.server:match('^([^:/]+):(%d+)$')
  if not host then
    self.message = 'informe o endereço como host:porta (ex.: 127.0.0.1:7801)'
    return
  end
  if not self.token or self.token == '' then
    self.message = 'o token aparece no link loomia:// do site'
    return
  end
  local args = {
    host = host,
    port = tonumber(port),
    server = ('%s:%d'):format(host, tonumber(port)),
    token = self.token,
    name = self.name,
    origin = self.origin,
    color = 'eabe19',
    valid = true,
  }
  self.app.loading:start(args)
  self.app.state = 'loading'
end

function menu:update(dt)
  self.caret = (self.caret + dt) % 1
end

local FIELDS = { { key = 'server', label = 'Sala (host:porta)' }, { key = 'token', label = 'Token da sala' }, { key = 'name', label = 'Seu nome' }, { key = 'origin', label = 'Site (http://…)' } }

function menu:draw(pass)
  local width, height = lovr.graphics.getDimensions()
  pass:setColor(0.09, 0.13, 0.22, 1)
  pass:clear()

  local font = lovr.graphics.getDefaultFont()
  font:setPixelSize(46)
  pass:setFont(font)
  pass:setColor(0.45, 0.85, 1, 1)
  pass:text('LOOMIA', width / 2, height - 120, 0.5, 0, 0, 0, 0.5)

  font:setPixelSize(18)
  pass:setFont(font)
  pass:setColor(0.8, 0.85, 0.92, 1)
  pass:text('Informe a sala para entrar (ou abra o jogo pelo site).', width / 2, height - 160, 0.5, 0, 0, 0, 0.5)

  local y = height - 230
  for _, entry in ipairs(FIELDS) do
    local selected = self.field == entry.key
    local text = self[entry.key] or ''
    if selected and self.caret < 0.5 then text = text .. '|' end
    pass:setColor(selected and 0.45 or 0.6, selected and 0.85 or 0.65, selected and 1 or 0.7, 1)
    pass:text(entry.label .. ': ' .. text, width / 2, y, 0.5, 0, 0, 0, 0.5)
    y = y - 40
  end

  pass:setColor(0.55, 0.6, 0.68, 1)
  pass:text('Tab: próximo campo · Enter: entrar · Esc: sair', width / 2, y - 20, 0.5, 0, 0, 0, 0.5)

  if self.message then
    pass:setColor(1, 0.55, 0.45, 1)
    pass:text(self.message, width / 2, y - 60, 0.5, 0, 0, 0, 0.5)
  end
end

function menu:textinput(text)
  self[self.field] = (self[self.field] or '') .. text
end

function menu:keypressed(key)
  if key == 'tab' then
    for index, entry in ipairs(FIELDS) do
      if entry.key == self.field then
        self.field = FIELDS[index % #FIELDS + 1].key
        return
      end
    end
  elseif key == 'backspace' then
    local value = self[self.field] or ''
    self[self.field] = value:sub(1, -2)
  elseif key == 'return' then
    if self.field ~= 'name' then
      submit(self)
    end
  elseif key == 'escape' then
    lovr.event.quit()
  end
end

function menu:mousemoved() end

return menu
