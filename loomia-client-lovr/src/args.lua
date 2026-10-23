-- Argumentos de inicialização do cliente.
--
-- Dois formatos são aceitos:
--   • pares `chave=valor` (é o que o handler `loomia://` entrega ao LOVR):
--         lovr loomia-client-lovr server=127.0.0.1:7801 token=abc name=Ana
--   • o próprio link como argumento (copiado/colado do site):
--         lovr loomia-client-lovr "loomia://launch?server=127.0.0.1:7801&token=abc"

local args = {
  server = nil,
  host = nil,
  port = nil,
  token = nil,
  map = nil,
  name = 'Loomian',
  color = 'eabe19',
  origin = nil,
  valid = false,
  error = nil,
}

local function urlDecode(text)
  text = text:gsub('+', ' ')
  return (text:gsub('%%(%x%x)', function(hex)
    return string.char(tonumber(hex, 16))
  end))
end

local function setServer(value)
  if not value or value == '' then return end
  local host, port = value:match('^([^:/]+):(%d+)$')
  if host then
    args.host, args.port = host, tonumber(port)
  else
    args.host, args.port = value, 7801
  end
  args.server = ('%s:%d'):format(args.host, args.port)
end

local function applyPair(key, value)
  value = urlDecode(value)
  if key == 'server' then
    setServer(value)
  elseif key == 'host' then
    args.host = value
  elseif key == 'port' then
    args.port = tonumber(value)
  elseif key == 'token' then
    args.token = value
  elseif key == 'map' then
    args.map = value
  elseif key == 'name' then
    args.name = value:sub(1, 20)
  elseif key == 'color' then
    args.color = value:gsub('#', '')
  elseif key == 'origin' then
    args.origin = value
  end
end

local function parseLink(link)
  local query = link:match('%?(.*)$') or ''
  for pair in query:gmatch('[^&]+') do
    local key, value = pair:match('^([^=]+)=(.*)$')
    if key then applyPair(key, value) end
  end
end

function args.parse(argv)
  argv = argv or lovr.arg or {}
  for _, raw in ipairs(argv) do
    local value = tostring(raw)
    if value:match('^loomia:') then
      parseLink(value)
    else
      local key, rest = value:match('^(%w+)=(.*)$')
      if key then applyPair(key, rest) end
    end
  end

  if args.host and not args.port then args.port = 7801 end
  if args.host and args.port then args.server = ('%s:%d'):format(args.host, args.port) end

  if not args.server then
    args.error = 'sem servidor no argumento (abra pelo site ou digite no menu)'
  elseif not args.token or args.token == '' then
    args.error = 'o link não trouxe o token da sala'
  else
    args.valid = true
  end
  return args
end

return args.parse()
