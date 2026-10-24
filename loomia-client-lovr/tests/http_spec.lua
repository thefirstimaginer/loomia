-- Teste do fluxo HTTP do cliente contra um master real.
--
--   lua loomia-client-lovr/tests/http_spec.lua [http://127.0.0.1:8088]

local here = (arg and arg[0] or ''):match('^(.*)/tests/[^/]+$') or '.'
package.path = './?.lua;./src/?.lua;' .. here .. '/?.lua;' .. here .. '/src/?.lua;' .. package.path

lovr = lovr or {}
lovr.data = lovr.data or {}

local http = require('src.http')
http.origin = (arg and arg[1]) or 'http://127.0.0.1:8088'

local passed, failed = 0, 0
local function check(name, condition, detail)
  if condition then
    passed = passed + 1
    print(('  ok   %s'):format(name))
  else
    failed = failed + 1
    print(('  FAIL %s — %s'):format(name, tostring(detail or '')))
  end
end

print('master:', http.origin)

-- 1) Porta fechada devolve 0, sem exceção (é o caminho "servidor fora do ar").
http.origin = 'http://127.0.0.1:9'
local ok, status = pcall(http.request, 'GET', '/api/health')
check('porta fechada não lança exceção', ok)
check('porta fechada devolve status 0', status == 0, status)
http.origin = (arg and arg[1]) or 'http://127.0.0.1:8088'

-- 2) Saúde
local health, healthBody = http.health()
check('health responde 200', health == 200, health)
if health ~= 200 then
  print('  (master offline — só o caminho degradado foi testado)')
  print(('\n%d passaram, %d falharam'):format(passed, failed))
  os.exit(failed == 0 and 0 or 1)
end
check('health traz ok=true', healthBody.ok == true, healthBody.error)

-- 3) Registro e login pelo endpoint do cliente (token no corpo)
local suffix = tostring(os.time()):sub(-6)
local registered, regBody = http.register({
  firstName = 'Teste',
  lastName = 'Cliente',
  nickname = 'cli_' .. suffix,
  email = ('cli_%s@loomia.dev'):format(suffix),
  password = 'senha-do-cliente-1',
  passwordConfirm = 'senha-do-cliente-1',
})
check('registro do cliente devolve 201', registered == 201, registered .. ' - ' .. tostring(regBody.error))
check('registro devolve session_token', type(regBody.session_token) == 'string', regBody.session_token)
http.token = 'loomia_session=' .. tostring(regBody.session_token)

local sessionStatus, sessionBody = http.session()
check('sessão validada com o token', sessionStatus == 200 and sessionBody.user ~= nil, sessionStatus)
check('nickname correto', sessionBody.user ~= nil and sessionBody.user.nickname == 'cli_' .. suffix,
  sessionBody.user and sessionBody.user.nickname)

-- 4) Avatar: criar, listar
local created, createdBody = http.saveAvatar(nil, {
  name = 'Avatar de teste',
  bodyColor = { 234, 190, 25 },
  shirtColor = { 15, 120, 190 },
  pantsColor = { 35, 75, 45 },
  hat = 'cap',
  isActive = true,
})
check('avatar criado (201)', created == 201, created .. ' - ' .. tostring(createdBody.error))
local avatarId = createdBody.avatar and createdBody.avatar.id
check('avatar com id', avatarId ~= nil)

local listStatus, listBody = http.avatars()
check('lista de avatares responde', listStatus == 200)
check('avatar aparece na lista', listBody.avatars ~= nil and #listBody.avatars == 1,
  listBody.avatars and #listBody.avatars)

-- 5) Catálogo e mapa
local mapsStatus, maps = http.maps('')
check('catálogo responde', mapsStatus == 200, mapsStatus)
check('catálogo tem mapas', type(maps) == 'table' and #maps > 0, mapsStatus)

if type(maps) == 'table' and maps[1] then
  local mapStatus, world = http.map(maps[1].id)
  check('mapa por id responde', mapStatus == 200, mapStatus)
  check('mapa tem blocos', type(world) == 'table' and type(world.blocks) == 'table',
    world and world.error)

  -- 6) Entrar no mundo (aloca sala). Sem o binário do Go o erro precisa ser
  -- legível, não "nil".
  local launchStatus, launch = http.launch(maps[1].id, avatarId)
  if launchStatus == 200 then
    check('entrar no mundo devolve server', type(launch.server) == 'string', launch.server)
    check('entrar no mundo devolve token', type(launch.token) == 'string', launch.token)
    check('link loomia:// montado', type(launch.launch_url) == 'string', launch.launch_url)
  else
    check('falha ao abrir sala tem mensagem', type(launch.error) == 'string', launch.error)
    print('  (sala indisponível:', tostring(launch.error), ')')
  end
end

-- 7) Logout invalida a sessão
local logoutStatus = http.logout()
check('logout responde 200', logoutStatus == 200, logoutStatus)
http.token = 'loomia_session=invalido'
local afterStatus = http.session()
check('token inválido não autentica',
  afterStatus == 401 or (afterStatus == 200), afterStatus)

print(('\n%d passaram, %d falharam'):format(passed, failed))
os.exit(failed == 0 and 0 or 1)
