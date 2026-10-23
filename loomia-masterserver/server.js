// Servidor master do Loomia (Node.js, somente módulos nativos).
//
// Responsabilidades:
//   • servir o site (frontend estático) e a API REST;
//   • contas e aparência dos jogadores;
//   • catálogo de mapas publicados pela comunidade;
//   • alocar salas: sobe um processo Go por mapa e devolve o link `loomia://`;
//   • WebSocket do lobby (presença das salas em tempo real);
//   • registrar o protocolo `loomia://` na máquina (modo dev).
//
// O master NÃO simula o jogo: física e snapshots são do game server em Go.

import http from 'node:http'
import fsp from 'node:fs/promises'
import path from 'node:path'
import crypto from 'node:crypto'

import { config } from './lib/config.js'
import { Store } from './lib/store.js'
import { RoomManager, launchUrl } from './lib/rooms.js'
import { installProtocolHandler } from './lib/protocol.js'
import { seedWorlds } from './lib/seed.js'
import { sendJson, sendError, readBody, randomToken } from './lib/util.js'

const startedAt = Date.now()

// --------------------------------------------------------------------------
// Log
// --------------------------------------------------------------------------

const levels = { debug: 10, info: 20, warn: 30, error: 40 }
const logLevel = levels[process.env.LOOMIA_LOG ?? 'info'] ?? 20

const log = {
  debug: (...args) => logLevel <= 10 && console.debug('[debug]', ...args),
  info: (...args) => logLevel <= 20 && console.log('[info]', ...args),
  warn: (...args) => logLevel <= 30 && console.warn('[warn]', ...args),
  error: (...args) => logLevel <= 40 && console.error('[error]', ...args),
}

// --------------------------------------------------------------------------
// Estado (sessões e lobby)
// --------------------------------------------------------------------------

const store = await new Store(config.dataDir).load()
await seedWorlds(store, log)
const rooms = new RoomManager({ config, store, log })
const launch = await installProtocolHandler({ config, log })

/** token do site -> { userId, name, expiresAt } */
const sessions = new Map()
const stats = { httpRequests: 0, launches: 0, registrations: 0, connects: 0 }

function publicUser(user) {
  return { id: user.id, name: user.name, appearance: user.appearance ?? null }
}

function sessionUser(req) {
  const header = req.headers.authorization ?? ''
  const token = header.startsWith('Bearer ') ? header.slice(7) : req.headers['x-loomia-session']
  if (!token) return null
  const session = sessions.get(token)
  if (!session || session.expiresAt < Date.now()) {
    if (session) sessions.delete(token)
    return null
  }
  session.expiresAt = Date.now() + 7 * 24 * 3600 * 1000
  const user = store.data.users.find((entry) => entry.id === session.userId)
  return user ? { user, token } : null
}

// --------------------------------------------------------------------------
// Rate limit simples em memória (por IP)
// --------------------------------------------------------------------------

const buckets = new Map()
function rateLimit(key, max, windowMs) {
  const now = Date.now()
  const bucket = buckets.get(key)
  if (!bucket || bucket.reset < now) {
    buckets.set(key, { count: 1, reset: now + windowMs })
    return true
  }
  bucket.count += 1
  return bucket.count <= max
}

function clientIp(req) {
  return req.socket.remoteAddress ?? 'desconhecido'
}

// --------------------------------------------------------------------------
// Rotas da API
// --------------------------------------------------------------------------

const routes = [
  ['GET', /^\/api\/health$/, handleHealth],
  ['GET', /^\/api\/status$/, handleStatus],
  ['POST', /^\/api\/auth\/register$/, handleRegister],
  ['POST', /^\/api\/auth\/login$/, handleLogin],
  ['POST', /^\/api\/auth\/logout$/, handleLogout],
  ['GET', /^\/api\/me$/, handleMe],
  ['PUT', /^\/api\/me\/appearance$/, handleAppearance],
  ['GET', /^\/api\/maps$/, handleListMaps],
  ['GET', /^\/api\/maps\/([\w-]+)$/, handleGetMap],
  ['POST', /^\/api\/maps$/, handleCreateMap],
  ['GET', /^\/api\/players$/, handlePlayers],
  ['GET', /^\/api\/rooms$/, handleRooms],
  ['POST', /^\/api\/rooms$/, handleOpenRoom],
  ['GET', /^\/api\/rooms\/([\w-]+)$/, handleRoom],
  ['POST', /^\/api\/launch$/, handleLaunch],
  ['GET', /^\/api\/launch\/info$/, handleLaunchInfo],
  ['POST', /^\/api\/internal\/rooms\/report$/, handleRoomReport],
  ['POST', /^\/api\/internal\/rooms\/close$/, handleRoomClose],
]

async function handleHealth(req, res, _arg, match) {
  void req
  void match
  sendJson(res, 200, { ok: true, uptime_secs: Math.floor((Date.now() - startedAt) / 1000) })
}

function handleStatus(req, res) {
  void req
  const roomList = rooms.list()
  const players = roomList.reduce((total, room) => total + room.players, 0)
  sendJson(res, 200, {
    name: config.serverName,
    version: '1.0.0',
    uptime_secs: Math.floor((Date.now() - startedAt) / 1000),
    tick_rate: 30,
    players_online: players,
    sessions: sessions.size,
    maps: store.listMaps().length,
    rooms: roomList.map((room) => ({
      map_id: room.map_id,
      map_name: room.map_name,
      players: room.players,
      capacity: room.capacity,
      address: `${room.host}:${room.port}`,
      started_at: room.started_at,
    })),
    protocol: {
      scheme: 'loomia://',
      handler: launch.handlerPath,
      registered: launch.registered,
    },
    stats,
  })
}

async function handleRegister(req, res, _arg, match) {
  void match
  if (!rateLimit(`register:${clientIp(req)}`, 10, 60_000)) {
    return sendError(res, 429, 'muitas tentativas, espere um pouco')
  }
  const body = await readBody(req)
  const name = String(body.name ?? '').trim()
  const password = String(body.password ?? '')
  if (!/^[\w .-]{3,20}$/u.test(name)) {
    return sendError(res, 400, 'nome de 3 a 20 caracteres (letras, números, espaço, ponto, - e _)')
  }
  if (password.length < 6 || password.length > 100) {
    return sendError(res, 400, 'a senha precisa ter entre 6 e 100 caracteres')
  }
  try {
    const user = await store.register(name, password)
    stats.registrations += 1
    emitLobby({ type: 'userJoined', user: publicUser(user) })
    sendJson(res, 201, { user: publicUser(user) })
  } catch (error) {
    sendError(res, 409, error.message)
  }
}

async function handleLogin(req, res, _arg, match) {
  void match
  if (!rateLimit(`login:${clientIp(req)}`, 20, 60_000)) {
    return sendError(res, 429, 'muitas tentativas, espere um pouco')
  }
  const body = await readBody(req)
  const user = store.authenticate(body.name, String(body.password ?? ''))
  if (!user) return sendError(res, 401, 'nome ou senha inválidos')
  const token = randomToken(24)
  sessions.set(token, { userId: user.id, expiresAt: Date.now() + 7 * 24 * 3600 * 1000 })
  sendJson(res, 200, { token, user: publicUser(user) })
}

function handleLogout(req, res, _arg, match) {
  void match
  const session = sessionUser(req)
  if (session) sessions.delete(session.token)
  sendJson(res, 200, { ok: true })
}

function handleMe(req, res, _arg, match) {
  void match
  const session = sessionUser(req)
  if (!session) return sendError(res, 401, 'não autenticado')
  sendJson(res, 200, { user: publicUser(session.user) })
}

async function handleAppearance(req, res, _arg, match) {
  void match
  const session = sessionUser(req)
  if (!session) return sendError(res, 401, 'não autenticado')
  const body = await readBody(req)
  const appearance = sanitizeAppearance(body, session.user.name)
  const user = await store.setAppearance(session.user.id, appearance)
  sendJson(res, 200, { user: publicUser(user) })
}

function sanitizeAppearance(body, fallbackName) {
  const color = (value, fallback) => {
    if (!Array.isArray(value) || value.length !== 3) return fallback
    return value.map((entry) => Math.max(0, Math.min(255, Math.round(Number(entry) || 0))))
  }
  const hats = ['', 'cap', 'top_hat', 'propeller', 'crown']
  return {
    username: String(body.username ?? fallbackName).slice(0, 20) || fallbackName,
    body_color: color(body.body_color, [234, 190, 25]),
    shirt_color: color(body.shirt_color, [15, 120, 190]),
    pants_color: color(body.pants_color, [35, 75, 45]),
    hat: hats.includes(body.hat) && body.hat !== '' ? body.hat : null,
  }
}

async function handleListMaps(req, res, _arg, match) {
  void req
  void match
  const maps = store.listMaps()
  const open = new Map(rooms.list().map((room) => [room.map_id, room]))
  for (const map of maps) {
    const room = open.get(map.id)
    map.online = room ? room.players : 0
    map.room_open = Boolean(room)
    map.max_players = room ? room.capacity : map.max_players
  }
  sendJson(res, 200, maps)
}

async function handleGetMap(_req, res, mapId, match) {
  void match
  const world = await store.readWorld(mapId)
  if (!world) return sendError(res, 404, 'mapa não encontrado')
  sendJson(res, 200, { ...world, version: store.worldVersion(world) })
}

async function handleCreateMap(req, res, _arg, match) {
  void match
  const session = sessionUser(req)
  if (!session) return sendError(res, 401, 'entre na sua conta para publicar um mapa')
  const body = await readBody(req, 8 * 1024 * 1024)
  const world = body.map ?? body
  if (!world || typeof world.id !== 'string' || !Array.isArray(world.blocks)) {
    return sendError(res, 400, 'envie {"map": {"id": "...", "name": "...", "blocks": [...]}}')
  }
  if (!/^[\w-]{2,40}$/.test(world.id)) {
    return sendError(res, 400, 'id do mapa inválido (use letras, números e -)')
  }
  if (world.blocks.length > 200_000) {
    return sendError(res, 413, 'mapa com blocos demais (limite 200 mil)')
  }
  const map = await store.saveWorld(world, session.user.name)
  emitLobby({ type: 'mapPublished', map: store.publicMapMeta(map) })
  sendJson(res, 201, store.publicMapMeta(map))
}

function handlePlayers(req, res, _arg, match) {
  void req
  void match
  const players = []
  for (const room of rooms.rooms.values()) {
    for (const name of room.playerNames) {
      players.push({ name, map_id: room.mapId, map_name: room.mapName })
    }
  }
  sendJson(res, 200, { rooms: rooms.list(), players })
}

async function handleRooms(req, res, _arg, match) {
  void req
  void match
  sendJson(res, 200, rooms.list())
}

async function handleOpenRoom(req, res, _arg, match) {
  void match
  const body = await readBody(req)
  const mapId = String(body.map_id ?? '')
  if (!store.findMap(mapId)) return sendError(res, 404, 'mapa não encontrado')
  try {
    const room = await rooms.allocate(mapId)
    sendJson(res, 200, room)
  } catch (error) {
    sendError(res, 503, error.message)
  }
}

async function handleRoom(req, res, mapId, match) {
  void match
  const room = rooms.find(mapId)
  if (!room) return sendError(res, 404, 'sala não está aberta')
  const url = new URL(req.url, 'http://localhost')
  const token = url.searchParams.get('token') ?? ''
  if (token) {
    const valid = rooms.validateJoin(mapId, token)
    if (!valid) return sendError(res, 401, 'token de entrada inválido ou expirado')
  }
  sendJson(res, 200, rooms.publicRoom(room))
}

/**
 * O botão "Jogar" do site chama isto. Devolve o link `loomia://` que o
 * navegador abre para acionar o cliente LOVR no computador do jogador.
 */
async function handleLaunch(req, res, _arg, match) {
  void match
  if (!rateLimit(`launch:${clientIp(req)}`, 30, 60_000)) {
    return sendError(res, 429, 'muitos lançamentos, espere um pouco')
  }
  const body = await readBody(req)
  const mapId = String(body.map_id ?? body.mapId ?? '')
  const map = store.findMap(mapId)
  if (!map) return sendError(res, 404, 'mapa não encontrado')

  const session = sessionUser(req)
  const playerName = String(body.name ?? session?.user.name ?? 'Loomian').slice(0, 20) || 'Loomian'

  try {
    const room = await rooms.allocate(mapId)
    const origin = `${req.headers['x-forwarded-proto'] ?? 'http'}://${req.headers.host}/`
    const link = launchUrl(
      { host: room.host, port: room.port, launchToken: room.launch_token, mapId },
      {
        playerName,
        color: typeof body.color === 'string' ? body.color.replace('#', '') : 'eabe19',
        origin,
      },
    )
    stats.launches += 1
    sendJson(res, 200, {
      ok: true,
      map_id: mapId,
      server: `${room.host}:${room.port}`,
      token: room.launch_token,
      launch_url: link,
      message: `Sala de "${map.name}" pronta em ${room.host}:${room.port}`,
    })
  } catch (error) {
    sendError(res, 503, error.message)
  }
}

function handleLaunchInfo(req, res, _arg, match) {
  void req
  void match
  sendJson(res, 200, {
    scheme: 'loomia://',
    handler: launch.handlerPath,
    registered: launch.registered,
    lovr: config.lovrBin,
    client_dir: config.clientDir,
    platform: process.platform,
  })
}

// Relatórios internos dos game servers (autenticados pelo segredo compartilhado).
async function handleRoomReport(req, res, _arg, match) {
  void match
  const body = await readBody(req)
  if (body.secret !== config.roomSecret) return sendError(res, 403, 'segredo inválido')
  const room = rooms.report(body)
  if (!room) return sendError(res, 404, 'sala desconhecida')
  if (body.chat?.text) {
    emitLobby({ type: 'chat', room: room.mapId, from: body.chat.from, text: body.chat.text })
  }
  emitLobby({ type: 'rooms', rooms: rooms.list() })
  sendJson(res, 200, { ok: true })
}

async function handleRoomClose(req, res, _arg, match) {
  void match
  const body = await readBody(req)
  if (body.secret !== config.roomSecret) return sendError(res, 403, 'segredo inválido')
  await rooms.close(Number(body.session))
  emitLobby({ type: 'rooms', rooms: rooms.list() })
  sendJson(res, 200, { ok: true })
}

// --------------------------------------------------------------------------
// WebSocket do lobby (RFC 6455 mínimo: sem dependências)
// --------------------------------------------------------------------------

const lobbyClients = new Set()

function wsAccept(key) {
  return crypto
    .createHash('sha1')
    .update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11')
    .digest('base64')
}

function wsFrame(text) {
  const payload = Buffer.from(text)
  const length = payload.length
  let header
  if (length < 126) {
    header = Buffer.from([0x81, length])
  } else if (length < 65536) {
    header = Buffer.alloc(4)
    header[0] = 0x81
    header[1] = 126
    header.writeUInt16BE(length, 2)
  } else {
    header = Buffer.alloc(10)
    header[0] = 0x81
    header[1] = 127
    header.writeBigUInt64BE(BigInt(length), 2)
  }
  return Buffer.concat([header, payload])
}

function emitLobby(message) {
  const frame = wsFrame(JSON.stringify(message))
  for (const socket of lobbyClients) {
    socket.write(frame)
  }
}

function handleWebSocket(req, socket) {
  const key = req.headers['sec-websocket-key']
  if (!key) {
    socket.end('HTTP/1.1 400 Bad Request\r\n\r\n')
    return
  }
  socket.write(
    'HTTP/1.1 101 Switching Protocols\r\n' +
      'Upgrade: websocket\r\n' +
      'Connection: Upgrade\r\n' +
      `Sec-WebSocket-Accept: ${wsAccept(key)}\r\n\r\n`,
  )
  socket.setNoDelay(true)
  lobbyClients.add(socket)
  stats.connects += 1
  socket.write(wsFrame(JSON.stringify({ type: 'hello', rooms: rooms.list(), stats })))

  socket.on('data', (buffer) => {
    if (buffer.length < 2) return
    const opcode = buffer[0] & 0x0f
    if (opcode === 0x8) {
      lobbyClients.delete(socket)
      socket.end()
      return
    }
    if (opcode === 0x9) {
      socket.write(Buffer.from([0x8a, 0]))
    }
  })
  socket.on('close', () => lobbyClients.delete(socket))
  socket.on('error', () => lobbyClients.delete(socket))
}

// --------------------------------------------------------------------------
// Arquivos estáticos do site
// --------------------------------------------------------------------------

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.ico': 'image/x-icon',
  '.woff2': 'font/woff2',
  '.txt': 'text/plain; charset=utf-8',
  '.sh': 'text/x-shellscript; charset=utf-8',
}

async function serveStatic(req, res, pathname) {
  void req
  const root = config.frontendDir
  const relative = pathname === '/' ? 'index.html' : decodeURIComponent(pathname).replace(/^\/+/, '')
  let file = path.join(root, relative)
  if (!file.startsWith(root)) {
    return sendError(res, 403, 'caminho inválido')
  }
  try {
    const stat = await fsp.stat(file)
    if (stat.isDirectory()) file = path.join(file, 'index.html')
  } catch {
    // SPA: qualquer rota desconhecida cai no index.html.
    file = path.join(root, 'index.html')
  }
  try {
    const data = await fsp.readFile(file)
    res.writeHead(200, {
      'content-type': MIME[path.extname(file)] ?? 'application/octet-stream',
      'content-length': data.length,
      'cache-control': file.endsWith('index.html') ? 'no-cache' : 'public, max-age=3600',
    })
    res.end(data)
  } catch {
    res.writeHead(503, { 'content-type': 'text/plain; charset=utf-8' })
    res.end(
      'O site ainda não foi compilado.\nRode: cd loomia-frontend && npm install && npm run build\n',
    )
  }
}

// --------------------------------------------------------------------------
// Servidor HTTP
// --------------------------------------------------------------------------

const server = http.createServer(async (req, res) => {
  stats.httpRequests += 1
  const url = new URL(req.url ?? '/', `http://${req.headers.host ?? 'localhost'}`)

  // CORS para o `npm run dev` (Vite em outra porta).
  res.setHeader('access-control-allow-origin', req.headers.origin ?? '*')
  res.setHeader('access-control-allow-headers', 'content-type, authorization, x-loomia-session')
  res.setHeader('access-control-allow-methods', 'GET, POST, PUT, DELETE, OPTIONS')
  if (req.method === 'OPTIONS') {
    res.writeHead(204)
    return res.end()
  }

  // Porta do game server: o site anuncia onde ele está escutando.
  if (url.pathname === '/api/connections') {
    return sendJson(res, 200, {
      server: `${config.host}:${config.port}`,
      rooms: rooms.list().map((room) => `${room.host}:${room.port}`),
    })
  }

  for (const [method, pattern, handler] of routes) {
    const match = url.pathname.match(pattern)
    if (!match || req.method !== method) continue
    try {
      // Os handlers recebem o próprio array do match (grupos em [1..]).
      await handler(req, res, match[1], match)
    } catch (error) {
      log.error(`${req.method} ${url.pathname}: ${error.message}`)
      if (!res.headersSent) sendError(res, 500, error.message)
    }
    return
  }

  if (url.pathname.startsWith('/api/')) {
    return sendError(res, 404, 'rota não encontrada')
  }
  return serveStatic(req, res, url.pathname)
})

// WebSocket do lobby: mesmo upgrade handler para /ws e /lobby.
server.on('upgrade', (req, socket) => {
  const url = new URL(req.url ?? '/', 'http://localhost')
  if (url.pathname === '/ws' || url.pathname === '/lobby') {
    return handleWebSocket(req, socket)
  }
  socket.end('HTTP/1.1 404 Not Found\r\n\r\n')
})

// Envia a presença das salas para o site a cada 3 s.
setInterval(() => emitLobby({ type: 'rooms', rooms: rooms.list() }), 3000).unref()

server.listen(config.port, process.env.LOOMIA_BIND ?? config.host, () => {
  log.info(`${config.serverName} master em http://${config.host}:${config.port} (escuta em ${process.env.LOOMIA_BIND ?? config.host})`)
  log.info(`site: ${config.frontendDir}`)
  log.info(`dados: ${config.dataDir}`)
})

function shutdown() {
  log.info('encerrando…')
  rooms.shutdown()
  server.close(() => process.exit(0))
  setTimeout(() => process.exit(0), 2000).unref()
}
process.on('SIGINT', shutdown)
process.on('SIGTERM', shutdown)
