-- JSON mínimo (encode/decode) para quando nem o LOVR nem o LuaSocket/cjson
-- estiverem disponíveis. Cobre apenas objetos, arrays, strings, números,
-- booleanos e null — o suficiente para o protocolo do jogo.

local json = {}

local escapes = {
  ['"'] = '\\"', ['\\'] = '\\\\', ['\b'] = '\\b', ['\f'] = '\\f',
  ['\n'] = '\\n', ['\r'] = '\\r', ['\t'] = '\\t',
}

local function escapeString(text)
  return (tostring(text):gsub('[%c"\\]', function(char)
    return escapes[char] or ('\\u%04x'):format(char:byte())
  end))
end

local function encodeValue(value, out)
  local kind = type(value)
  if kind == 'nil' then
    out[#out + 1] = 'null'
  elseif kind == 'boolean' then
    out[#out + 1] = value and 'true' or 'false'
  elseif kind == 'number' then
    if value ~= value or value == math.huge or value == -math.huge then
      out[#out + 1] = 'null'
    elseif value == math.floor(value) and math.abs(value) < 1e15 then
      out[#out + 1] = ('%d'):format(value)
    else
      out[#out + 1] = ('%.6f'):format(value):gsub('0+$', ''):gsub('%.$', '')
    end
  elseif kind == 'string' then
    out[#out + 1] = '"' .. escapeString(value) .. '"'
  elseif kind == 'table' then
    -- Array se todas as chaves forem inteiras 1..n; senão, objeto.
    local count = #value
    local isArray = count > 0
    if isArray then
      for key in pairs(value) do
        if type(key) ~= 'number' then
          isArray = false
          break
        end
      end
    end
    out[#out + 1] = isArray and '[' or '{'
    local first = true
    if isArray then
      for index = 1, count do
        if not first then out[#out + 1] = ',' end
        first = false
        encodeValue(value[index], out)
      end
    else
      for key, entry in pairs(value) do
        if not first then out[#out + 1] = ',' end
        first = false
        out[#out + 1] = '"' .. escapeString(key) .. '":'
        encodeValue(entry, out)
      end
    end
    out[#out + 1] = isArray and ']' or '}'
  else
    out[#out + 1] = 'null'
  end
end

function json.encode(value)
  local out = {}
  encodeValue(value, out)
  return table.concat(out)
end

-- Parser recursivo simples.
local function skip(text, index)
  while index <= #text do
    local char = text:sub(index, index)
    if char:match('%s') then
      index = index + 1
    else
      break
    end
  end
  return index
end

local parseValue

local function parseString(text, index)
  assert(text:sub(index, index) == '"', 'string esperada')
  index = index + 1
  local out = {}
  while index <= #text do
    local char = text:sub(index, index)
    if char == '"' then
      return table.concat(out), index + 1
    elseif char == '\\' then
      local nextChar = text:sub(index + 1, index + 1)
      local map = { n = '\n', t = '\t', r = '\r', b = '\b', f = '\f', ['"'] = '"', ['\\'] = '\\', ['/'] = '/' }
      if nextChar == 'u' then
        local hex = text:sub(index + 2, index + 5)
        local code = tonumber(hex, 16) or 63
        out[#out + 1] = utf8.char(code)
        index = index + 6
      else
        out[#out + 1] = map[nextChar] or nextChar
        index = index + 2
      end
    else
      out[#out + 1] = char
      index = index + 1
    end
  end
  error('string JSON sem fim')
end

function parseValue(text, index)
  index = skip(text, index)
  local char = text:sub(index, index)
  if char == '{' then
    local result = {}
    index = skip(text, index + 1)
    if text:sub(index, index) == '}' then return result, index + 1 end
    while true do
      local key
      key, index = parseString(text, skip(text, index))
      index = skip(text, index)
      assert(text:sub(index, index) == ':', ': esperado')
      local value
      value, index = parseValue(text, index + 1)
      result[key] = value
      index = skip(text, index)
      local separator = text:sub(index, index)
      index = index + 1
      if separator == '}' then return result, index end
      assert(separator == ',', ', ou } esperado')
    end
  elseif char == '[' then
    local result = {}
    index = skip(text, index + 1)
    if text:sub(index, index) == ']' then return result, index + 1 end
    while true do
      local value
      value, index = parseValue(text, index)
      result[#result + 1] = value
      index = skip(text, index)
      local separator = text:sub(index, index)
      index = index + 1
      if separator == ']' then return result, index end
      assert(separator == ',', ', ou ] esperado')
    end
  elseif char == '"' then
    return parseString(text, index)
  elseif text:sub(index, index + 3) == 'true' then
    return true, index + 4
  elseif text:sub(index, index + 4) == 'false' then
    return false, index + 5
  elseif text:sub(index, index + 3) == 'null' then
    return nil, index + 4
  else
    local number = text:match('^-?%d+%.?%d*[eE]?[-+]?%d*', index)
    assert(number, 'valor inválido na posição ' .. index)
    return tonumber(number), index + #number
  end
end

function json.decode(text)
  local value, index = parseValue(text, 1)
  index = skip(text, index)
  assert(index > #text or text:sub(index):match('^%s*$'), 'conteúdo extra no JSON')
  return value
end

return json
