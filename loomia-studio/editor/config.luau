--[[
	Configuração do Loomia Studio.

	Precedência: flags de linha de comando > padrão.
	O servidor é usado para buscar/salvar o `.loom` em `GET/POST /api/maps`.
]]

local Process = import("Process")

local config = {
	server = "http://127.0.0.1:8080",
	world = "baseplate",
	mapsDir = "maps",
	-- Autosave quando o servidor não responde (o arquivo é gravado localmente).
	offline = false,
}

local okArgs, args = pcall(function()
	return Process.Args()
end)
if okArgs and args then
	for i, value in ipairs(args) do
		if value == "--server" and args[i + 1] then
			config.server = args[i + 1]
		elseif value == "--world" and args[i + 1] then
			config.world = args[i + 1]
		elseif value == "--maps" and args[i + 1] then
			config.mapsDir = args[i + 1]
		elseif value == "--offline" then
			config.offline = true
		end
	end
end

config.server = config.server:gsub("/+$", "")

return config
