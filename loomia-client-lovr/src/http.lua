-- HTTP do cliente: fala com o master (login, avatares, catálogo, mapas).
--
-- O engine não tem cliente HTTP na API Lua, então os pedidos passam pelo
-- processo `curl` (presente em praticamente todo Linux). Tudo é BLOQUEANTE e
-- roda em corrotina para não travar o loop: os estados verdes "carregam" e o
-- desenho continua respondendo.
--
-- O cookie de sessão fica em memória e é enviado em todas as requisições.

local http = {}

http.origin = nil   -- ex.: 'http://127.0.0.1:8080'
http.token = nil    -- cookie de sessão do site (loomia_session=...)

local function quote(text)
  return '"' .. tostring(text):gsub('"', '\\"'):gsub('`', '\\`'):gsub('%$', '\\$') .. '"'
end

local function available(command)
  local handle = io.popen(('command -v %s 2>/dev/null'):format(command))
  if not handle then return false end
  local path = handle:read('*l')
  handle:close()
  return path ~= nil and path ~= ''
end

http.hasCurl = nil -- detectado no primeiro uso

function http.probe()
  if http.hasCurl == nil then
    http.hasCurl = available('curl')
  end
  return http.hasCurl
end

--- Constrói a URL completa a partir de um caminho da API.
function http.url(path)
  local origin = http.origin or ''
  origin = origin:gsub('/+$', '')
  return origin .. path
end

--- Executa um pedido e devolve `status, tabela (json)`.
-- `method` = 'GET' | 'POST' | 'PUT' | 'DELETE'
-- `body` = tabela (serializada como JSON) ou nil
function http.request(method, path, body)
  if not http.origin then
    return 0, { error = 'cliente sem endereço do servidor (origin)' }
  end
  if not http.probe() then
    return 0, { error = 'curl não encontrado no sistema (necessário para o site/API)' }
  end

  local protocol = require('src.protocol')
  local url = http.url(path)
  -- O corpo vai para um arquivo temporário e o status sai no stdout (`-w`).
  -- Assim corpo e código nunca se misturam (era o que fazia o JSON parecer
  -- inválido quando o status vinha colado nele).
  local bodyFile = os.tmpname()
  local parts = {
    'curl', '-sS', '-m', '15',
    '--connect-timeout', '5',
    '--stderr', '/dev/null',
    '-o', quote(bodyFile),
    '-w', quote('%{http_code}'),
    '-X', method,
    '-H', quote('content-type: application/json'),
  }
  if http.token and http.token ~= '' then
    parts[#parts + 1] = '-H'
    parts[#parts + 1] = quote('cookie: ' .. http.token)
  end
  if body ~= nil then
    parts[#parts + 1] = '--data-binary'
    parts[#parts + 1] = quote(protocol.encode(body))
  end
  parts[#parts + 1] = quote(url)

  local command = table.concat(parts, ' ')
  local handle = io.popen(command)
  if not handle then
    os.remove(bodyFile)
    return 0, { error = 'não foi possível executar o curl' }
  end

  -- `close` uma única vez: a segunda chamada explodia ("attempt to use a closed
  -- file") e derrubava o cliente quando o servidor estava fora do ar.
  local statusText = (handle:read('*a') or ''):match('(%d+)')
  local _, kind, code = handle:close()
  if type(kind) == 'number' then code = kind end

  local payload = ''
  local file = io.open(bodyFile, 'r')
  if file then
    payload = file:read('*a') or ''
    file:close()
  end
  os.remove(bodyFile)

  local status = tonumber(statusText) or tonumber(code) or 0


  -- Decodificação genérica de JSON: `protocol.decode` só aceita mensagens do
  -- jogo (exige campo `t`), então a API usa `lovr.data` ou o `json.lua`.
  local decoded = {}
  if payload and payload ~= '' then
    local ok, parsed = pcall(lovr.data.decode, 'json', payload)
    if not ok or type(parsed) ~= 'table' then
      ok, parsed = pcall(require('src.json').decode, payload)
    end
    if ok and type(parsed) == 'table' then decoded = parsed end
  end
  return status, decoded
end

-- ---------------------------------------------------------------------------
-- Endpoints usados pelo cliente
-- ---------------------------------------------------------------------------

function http.login(login, password)
  local status, body = http.request('POST', '/api/client/login', { login = login, password = password })
  if status == 200 and body.session_token then
    http.token = 'loomia_session=' .. body.session_token
  end
  return status, body
end
function http.register(fields)
  local status, body = http.request('POST', '/api/client/register', fields)
  if status == 201 and body.session_token then
    http.token = 'loomia_session=' .. body.session_token
  end
  return status, body
end

function http.session()
  return http.request('GET', '/api/auth/me')
end

function http.avatars()
  return http.request('GET', '/api/me/avatars')
end

function http.saveAvatar(id, avatar)
  if id then
    return http.request('PUT', '/api/me/avatars/' .. id, avatar)
  end
  return http.request('POST', '/api/me/avatars', avatar)
end

function http.activateAvatar(id)
  return http.request('POST', '/api/me/avatars/' .. id .. '/activate')
end

function http.deleteAvatar(id)
  return http.request('DELETE', '/api/me/avatars/' .. id)
end

function http.avatarOptions()
  return http.request('GET', '/api/avatar-options')
end

function http.maps(query)
  local suffix = (query and query ~= '') and ('?q=' .. tostring(query):gsub(' ', '%%20')) or ''
  return http.request('GET', '/api/maps' .. suffix)
end

function http.map(id)
  return http.request('GET', '/api/maps/' .. tostring(id))
end

function http.launch(mapId, avatarId)
  local body = { mapId = mapId }
  if avatarId then body.avatarId = avatarId end
  return http.request('POST', '/api/maps/' .. tostring(mapId) .. '/launch', body)
end

function http.logout()
  return http.request('POST', '/api/client/logout')
end

function http.health()
  return http.request('GET', '/api/health')
end

return http
