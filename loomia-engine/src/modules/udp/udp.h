/**
 * Módulo `udp` do Loomia Engine (API C).
 *
 * Sockets UDP sem bloqueio, direto sobre a API do sistema operacional — o
 * cliente Lua usa isto para falar com a sala (game server em Go) sem processo
 * auxiliar.
 *
 * Uso no Lua:
 *
 *   local udp = require('udp')
 *   udp.connect('127.0.0.1', 7801)              -- cria o socket da sessão
 *   udp.send('{"t":"hello",...}')               -- datagrama para o servidor
 *   local message, address = udp.receive()      -- um datagrama por chamada
 *   udp.port()                                  -- porta local (diagnóstico)
 *   udp.close()
 *
 * O `address` devolvido por `receive` é `"ip:porta"` (útil quando a sala muda
 * de endereço) e o conteúdo é a string JSON crua.
 *
 * Funções:
 *   udp.connect(host, port)   -> true | false, erro
 *   udp.send(data[, host, port]) -> bytes | nil, erro
 *   udp.receive()             -> data, address | nil, erro | nil, "timeout"
 *   udp.port()                -> porta local | nil
 *   udp.close()               -> nil
 */

#pragma once
#include "lua.h"

int luaopen_udp(lua_State* L);
