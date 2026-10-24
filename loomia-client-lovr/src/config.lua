-- Perfil local do jogador no cliente: sessão, avatares e preferências.
--
-- O token de sessão e o endereço do master ficam em `loomia-client.json` no
-- diretório de save do engine, então na próxima abertura o cliente já entra
-- logado (mesmo offline, mostra seu perfil e avatares em cache).

local config = {}

local FILE = 'loomia-client.json'

local function decode(text)
  local ok, value = pcall(lovr.data.decode, 'json', text)
  if ok and type(value) == 'table' then return value end
  local ok2, value2 = pcall(require('src.json').decode, text)
  if ok2 and type(value2) == 'table' then return value2 end
  return nil
end

local function encode(value)
  local ok, text = pcall(lovr.data.encode, value, 'json')
  if ok and text then return text end
  return require('src.json').encode(value)
end

--- Estado em memória (usado por toda a UI).
config.data = {
  origin = nil,          -- http://127.0.0.1:8080
  token = nil,           -- cookie de sessão
  user = nil,            -- { id, nickname, firstName, email, bio, role }
  avatars = {},          -- lista de avatares do servidor
  activeAvatarId = nil,
  lastMap = nil,
  offline = false,       -- último estado conhecido do master
}

function config.load(defaultOrigin)
  local file = io.open(lovr.filesystem.getSaveDirectory() .. '/' .. FILE, 'r')
  if file then
    local text = file:read('*a')
    file:close()
    local saved = decode(text)
    if saved then
      for key, value in pairs(saved) do
        config.data[key] = value
      end
    end
  end
  config.data.origin = config.data.origin or defaultOrigin
  return config.data
end

function config.save()
  local text = encode(config.data)
  local file = io.open(lovr.filesystem.getSaveDirectory() .. '/' .. FILE, 'w')
  if not file then return false end
  file:write(text)
  file:close()
  return true
end

function config.logout()
  config.data.token = nil
  config.data.user = nil
  config.data.avatars = {}
  config.data.activeAvatarId = nil
  config.save()
end

function config.activeAvatar()
  for _, avatar in ipairs(config.data.avatars or {}) do
    if avatar.isActive then return avatar end
  end
  for _, avatar in ipairs(config.data.avatars or {}) do
    if tostring(avatar.id) == tostring(config.data.activeAvatarId) then return avatar end
  end
  return (config.data.avatars or {})[1]
end

--- Converte o avatar da API (bodyColor [r,g,b]) para o formato do jogo.
function config.appearanceOf(avatar)
  if not avatar then
    return {
      username = (config.data.user and config.data.user.nickname) or 'Loomian',
      body_color = { 234, 190, 25 },
      shirt_color = { 15, 120, 190 },
      pants_color = { 35, 75, 45 },
      hat = nil,
    }
  end
  return {
    username = (config.data.user and config.data.user.nickname) or avatar.name or 'Loomian',
    body_color = avatar.bodyColor,
    shirt_color = avatar.shirtColor,
    pants_color = avatar.pantsColor,
    hat = avatar.hat,
  }
end

return config
