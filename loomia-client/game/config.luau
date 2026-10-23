--[[
	Configuração do cliente do Loomia.

	Tudo vem de argumentos de linha de comando (o servidor repassa em
	`loomia-client --server ... --name ...`). Valores padrão apontam para o
	servidor local.
]]

local Process = import("Process")
local Serde = import("Serde")

local config = {}

config.server = "ws://127.0.0.1:8080/ws"
config.playerName = "Socket7"
config.world = "baseplate"
config.appearance = nil

local okArgs, args = pcall(function()
	return Process.Args()
end)
if okArgs and args then
	for i, value in ipairs(args) do
		if value == "--server" and args[i + 1] then
			config.server = args[i + 1]
		elseif value == "--name" and args[i + 1] then
			config.playerName = args[i + 1]
		elseif value == "--world" and args[i + 1] then
			config.world = args[i + 1]
		elseif value == "--appearance" and args[i + 1] then
			local ok, decoded = pcall(function()
				return Serde.Decode("json", args[i + 1])
			end)
			if ok then
				config.appearance = decoded
			end
		end
	end
end

return config
