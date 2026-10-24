// Servidor do Loomia (master + site).
//
//   • API REST (src/routes.ts) com sessões por cookie
//   • serve o site compilado (loomia-frontend/dist) com fallback de SPA
//   • WebSocket /lobby: presença das salas em tempo real
//   • na inicialização: migrations do PostgreSQL + seed do catálogo
//
// Rodar:  npm run dev   (TS direto)  |  npm run build && npm start

import { createServer, type IncomingMessage, type ServerResponse } from 'node:http'
import { readFile, stat } from 'node:fs/promises'
import path from 'node:path'
import { createHash } from 'node:crypto'

import { config } from './config.js'
import { closeDatabase, migrate, pool } from './db/index.js'
import { seed } from './db/seed.js'
import { createApi } from './routes.js'
import { RoomManager } from './rooms.js'
import { HttpError, clientIp, parseCookies, readBody, sendJson } from './http.js'
import { createLogger } from './util.js'

const log = createLogger()
const startedAt = Date.now()

// ---------------------------------------------------------------------------
// Inicialização
// ---------------------------------------------------------------------------

// Erro de conexão com o banco não vira stack trace sem contexto: explica o que
// fazer e sai.
async function connectDatabase(): Promise<void> {
  try {
    // Primeiro só conecta: o erro de autenticação aparece antes de qualquer
    // coisa ser gravada no banco.
    await pool.query('select 1')
    const migrations = await migrate()
    if (migrations.length > 0) log.info(`migrations aplicadas: ${migrations.join(', ')}`)
    const seeded = await seed()
    if (seeded > 0) log.info(`catálogo: ${seeded} mapa(s) novo(s) a partir de data/worlds`)
  } catch (error) {
    const details = error as { code?: string; message?: string }
    const host = config.databaseUrl.replace(/:[^:@/]+@/, ':***@')
    log.error('não consegui usar o banco de dados')
    log.error(`  conexão: ${host}`)
    log.error(`  motivo: ${details.code ?? ''} ${details.message ?? error}`)
    if (details.code === '28P01' || details.code === '28000') {
      log.error('')
      log.error('  o usuário/senha do PostgreSQL não conferem.')
      log.error('  1) crie (ou conserte) o usuário e o banco, uma vez:')
      log.error('       sudo -u postgres createuser --login --pwprompt loomia')
      log.error('       sudo -u postgres createdb --owner=loomia loomia')
      log.error('     (ou rode: sudo -u postgres psql -f setup-database.sql)')
      log.error('  2) use a MESMA senha no DATABASE_URL:')
      log.error('       DATABASE_URL=postgresql://loomia:SUA_SENHA@127.0.0.1:5432/loomia npm start')
    } else if (details.code === '42501') {
      log.error('')
      log.error('  o usuário do banco não tem permissão para algo que a migration pediu.')
      log.error('  O Loomia não usa extensões; se aparecer "create extension",')
      log.error('  rode o setup de novo (ele aplica permissões no schema public):')
      log.error('       npm run db:setup')
    } else if (details.code === '3D000') {
      log.error('')
      log.error('  o banco "loomia" não existe. Crie com:')
      log.error('       sudo -u postgres createdb --owner=loomia loomia')
    } else if (details.code === 'ECONNREFUSED') {
      log.error('')
      log.error('  o PostgreSQL não está rodando (ou a porta está errada).')
      log.error('       sudo systemctl start postgresql')
    }
    process.exit(1)
  }
}

await connectDatabase()

const rooms = new RoomManager(config, log)
const api = createApi({ config, rooms, log, startedAt })

// ---------------------------------------------------------------------------
// WebSocket do lobby (RFC 6455 mínimo, sem dependências)
// ---------------------------------------------------------------------------

const lobbyClients = new Set<import('node:net').Socket>()

function wsAccept(key: string): string {
  return createHash('sha1').update(key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').digest('base64')
}

function wsFrame(text: string): Buffer {
  const payload = Buffer.from(text)
  const length = payload.length
  if (length < 126) return Buffer.concat([Buffer.from([0x81, length]), payload])
  if (length < 65536) {
    const header = Buffer.alloc(4)
    header[0] = 0x81
    header[1] = 126
    header.writeUInt16BE(length, 2)
    return Buffer.concat([header, payload])
  }
  const header = Buffer.alloc(10)
  header[0] = 0x81
  header[1] = 127
  header.writeBigUInt64BE(BigInt(length), 2)
  return Buffer.concat([header, payload])
}

function emitLobby(message: unknown): void {
  const frame = wsFrame(JSON.stringify(message))
  for (const socket of lobbyClients) socket.write(frame)
}

function handleUpgrade(req: IncomingMessage, socket: import('node:net').Socket): void {
  const key = req.headers['sec-websocket-key']
  if (!key) {
    socket.end('HTTP/1.1 400 Bad Request\r\n\r\n')
    return
  }
  socket.write(
    'HTTP/1.1 101 Switching Protocols\r\n' +
    'Upgrade: websocket\r\n' +
    'Connection: Upgrade\r\n' +
    `Sec-WebSocket-Accept: ${wsAccept(String(key))}\r\n\r\n`,
  )
  socket.setNoDelay(true)
  lobbyClients.add(socket)
  socket.write(wsFrame(JSON.stringify({ type: 'hello', rooms: rooms.list() })))
  socket.on('data', (buffer) => {
    const opcode = buffer[0]! & 0x0f
    if (opcode === 0x8) {
      lobbyClients.delete(socket)
      socket.end()
    } else if (opcode === 0x9) {
      socket.write(Buffer.from([0x8a, 0]))
    }
  })
  socket.on('close', () => lobbyClients.delete(socket))
  socket.on('error', () => lobbyClients.delete(socket))
}

const lobbyTimer = setInterval(() => emitLobby({ type: 'rooms', rooms: rooms.list() }), 3000)
lobbyTimer.unref()

// ---------------------------------------------------------------------------
// Arquivos estáticos (SPA)
// ---------------------------------------------------------------------------

const MIME: Record<string, string> = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.webp': 'image/webp',
  '.ico': 'image/x-icon',
  '.woff2': 'font/woff2',
  '.txt': 'text/plain; charset=utf-8',
}

async function serveStatic(res: ServerResponse, pathname: string): Promise<void> {
  const root = config.frontendDir
  const relative = pathname === '/' ? 'index.html' : decodeURIComponent(pathname).replace(/^\/+/, '')
  let file = path.join(root, relative)
  if (!file.startsWith(root)) {
    res.writeHead(403).end('caminho inválido')
    return
  }
  try {
    const info = await stat(file)
    if (info.isDirectory()) file = path.join(file, 'index.html')
  } catch {
    // SPA: rota desconhecida cai no index.html.
    file = path.join(root, 'index.html')
  }
  try {
    const data = await readFile(file)
    res.writeHead(200, {
      'content-type': MIME[path.extname(file)] ?? 'application/octet-stream',
      'content-length': data.length,
      'cache-control': file.endsWith('index.html') ? 'no-cache' : 'public, max-age=3600',
    })
    res.end(data)
  } catch {
    res.writeHead(503, { 'content-type': 'text/plain; charset=utf-8' })
    res.end('O site ainda não foi compilado.\nRode: cd loomia-frontend && npm install && npm run build\n')
  }
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------

const server = createServer(async (req, res) => {
  const url = new URL(req.url ?? '/', `http://${req.headers.host ?? 'localhost'}`)
  const ip = clientIp(req)

  // CORS para `npm run dev` do Vite (com credenciais, para o cookie de sessão).
  const origin = req.headers.origin
  if (origin) {
    res.setHeader('access-control-allow-origin', origin)
    res.setHeader('access-control-allow-credentials', 'true')
    res.setHeader('vary', 'origin')
  }
  res.setHeader('access-control-allow-headers', 'content-type')
  res.setHeader('access-control-allow-methods', 'GET, POST, PUT, DELETE, OPTIONS')

  if (req.method === 'OPTIONS') {
    res.writeHead(204).end()
    return
  }

  // Log curto de tempo/status.
  const startedRequest = Date.now()
  res.on('finish', () => {
    if (url.pathname.startsWith('/api/')) {
      log.debug(`${req.method} ${url.pathname} ${res.statusCode} ${Date.now() - startedRequest}ms`)
    }
  })

  if (url.pathname.startsWith('/api/')) {
    const route = api.match(req.method ?? 'GET', url.pathname)
    if (!route) {
      sendJson(res, 404, { error: 'rota não encontrada', code: 'not_found' })
      return
    }
    try {
      await route.handler({
        req,
        res,
        url,
        params: route.params,
        query: url.searchParams,
        cookies: parseCookies(req.headers.cookie),
        ip,
        body: <T = unknown>(limit?: number) => readBody<T>(req, limit),
      })
    } catch (error) {
      if (res.headersSent) {
        res.end()
        return
      }
      if (error instanceof HttpError) {
        sendJson(res, error.status, { error: error.message, code: error.code ?? 'error' })
      } else {
        log.error(`${req.method} ${url.pathname}:`, error)
        sendJson(res, 500, { error: 'erro interno do servidor', code: 'internal' })
      }
    }
    return
  }

  await serveStatic(res, url.pathname)
})

server.on('upgrade', (req, socket) => {
  const url = new URL(req.url ?? '/', 'http://localhost')
  if (url.pathname === '/lobby' || url.pathname === '/ws') {
    handleUpgrade(req, socket as import('node:net').Socket)
    return
  }
  socket.end('HTTP/1.1 404 Not Found\r\n\r\n')
})

server.listen(config.port, config.bind, () => {
  log.info(`${config.serverName} master em http://${config.host}:${config.port} (escuta ${config.bind})`)
  log.info(`site: ${config.frontendDir}`)
  log.info(`mapas: ${config.worldsDir}`)
  log.info(`banco: ${config.databaseUrl.replace(/:[^:@/]+@/, ':***@')}`)
})

async function shutdown(): Promise<void> {
  log.info('encerrando…')
  clearInterval(lobbyTimer)
  rooms.shutdown()
  server.close(() => { })
  await closeDatabase().catch(() => { })
  setTimeout(() => process.exit(0), 500).unref()
}

process.on('SIGINT', () => void shutdown())
process.on('SIGTERM', () => void shutdown())
