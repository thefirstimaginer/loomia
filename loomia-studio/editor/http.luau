--[[
	Cliente HTTP mínimo (via `Net.Request` da Ruzit) usado para trocar o `.loom`
	com o servidor do Loomia:

		GET  /api/maps/{id}   -> busca o mapa
		POST /api/maps        -> salva e faz o servidor recarregar o mundo

	`Net.Request(method, url, body?, headers?)` devolve `{ status, headers, body }`.
]]

local Http = {}
Http.__index = Http

local Net = import("Net")
local Serde = import("Serde")

function Http.new(baseUrl)
	return setmetatable({ base = baseUrl:gsub("/+$", "") }, Http)
end

local function ok(response)
	local status = tonumber(response.status) or 0
	return status >= 200 and status < 300
end

--- Busca o `.loom`. Devolve `world` ou nil, erro.
function Http:getMap(id)
	local called, response = pcall(function()
		return Net.Request("GET", self.base .. "/api/maps/" .. id, nil, nil)
	end)
	if not called then
		return nil, tostring(response)
	end
	if not ok(response) then
		return nil, "HTTP " .. tostring(response.status) .. " " .. tostring(response.body)
	end
	local decoded, world = pcall(function()
		return Serde.Decode("json", response.body)
	end)
	if not decoded or type(world) ~= "table" then
		return nil, "JSON inválido"
	end
	return world, nil
end

--- Salva no servidor (que grava o `.loom` e recarrega o mundo). ok, mensagem.
function Http:saveMap(world)
	local encoded, body = pcall(function()
		return Serde.Encode("json", world)
	end)
	if not encoded then
		return false, tostring(body)
	end
	local called, response = pcall(function()
		return Net.Request("POST", self.base .. "/api/maps", body, {
			["Content-Type"] = "application/json",
		})
	end)
	if not called then
		return false, tostring(response)
	end
	if not ok(response) then
		return false, "HTTP " .. tostring(response.status) .. " " .. tostring(response.body)
	end
	return true, "salvo no servidor"
end

return Http
