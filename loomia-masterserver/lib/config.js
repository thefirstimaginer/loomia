// Configuração do master server (variáveis de ambiente, com padrões de dev).
//
// Tudo o que é específico da máquina (caminhos para o binário do game server,
// o executável do LOVR e a pasta de dados) vive aqui, para o código de rotas
// ficar limpo.

import path from 'node:path'
import { fileURLToPath } from 'node:url'

const here = path.dirname(fileURLToPath(import.meta.url))
const root = path.resolve(here, '..')

const num = (value, fallback) => {
  const parsed = Number(value)
  return Number.isFinite(parsed) ? parsed : fallback
}

/**
 * Modo TCP: só para diagnóstico (quem tiver um cliente TCP). O jogo usa UDP.
 */
export const config = {
  root,
  host: process.env.LOOMIA_HOST ?? '127.0.0.1',
  port: num(process.env.LOOMIA_PORT ?? process.env.PORT, 8080),

  dataDir: path.resolve(root, process.env.LOOMIA_DATA_DIR ?? '../data'),
  frontendDir: path.resolve(root, '..', process.env.LOOMIA_FRONTEND_DIR ?? 'loomia-frontend/dist'),

  // Segredo que autentica os game servers nos endpoints /api/internal/*.
  roomSecret: process.env.LOOMIA_ROOM_SECRET ?? 'loomia-dev-secret',

  // Como iniciar uma sala. O build latest `go run` quando não há binário.
  gameServerBin: path.resolve(root, process.env.LOOMIA_GAMESERVER_BIN ?? '../loomia-gameserver/loomia-gameserver'),
  gameServerGoDir: path.resolve(root, '..', process.env.LOOMIA_GAMESERVER_DIR ?? 'loomia-gameserver'),
  gameServerGo: process.env.LOOMIA_GO ?? 'go',
  tcpMode: process.env.LOOMIA_TCP === '1',
  gameServerBind: process.env.LOOMIA_GAMESERVER_BIND ?? '127.0.0.1',
  gameServerPublicHost: process.env.LOOMIA_GAMESERVER_PUBLIC_HOST ?? '127.0.0.1',
  gameServerPortMin: num(process.env.LOOMIA_GAMESERVER_PORT_MIN, 7800),
  gameServerPortMax: num(process.env.LOOMIA_GAMESERVER_PORT_MAX, 7899),
  maxRooms: num(process.env.LOOMIA_MAX_ROOMS, 8),
  // Sala parada por tanto tempo sem jogadores é encerrada (ms).
  roomIdleMs: num(process.env.LOOMIA_ROOM_IDLE_MS, 2 * 60 * 1000),

  // Handler `loomia://`. `auto` registra na máquina do próprio servidor (dev).
  protocolHandler: process.env.LOOMIA_PROTOCOL_HANDLER ?? 'auto',
  lovrBin: process.env.LOVR ?? '/usr/bin/lovr',
  clientDir: path.resolve(root, '..', process.env.LOOMIA_LOVR_CLIENT_DIR ?? 'loomia-client-lovr'),

  // Token de entrada de uma sala expira depois disso (ms).
  launchTtlMs: num(process.env.LOOMIA_LAUNCH_TTL_MS, 30 * 1000),
  serverName: process.env.LOOMIA_SERVER_NAME ?? 'Loomia',
}
