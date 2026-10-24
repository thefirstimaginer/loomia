// Testes do painel admin, moderação e recursos sociais.
//
// PostgreSQL real + servidor em subprocesso (o fluxo depende de cookie).

import { test, before, after } from 'node:test'
import assert from 'node:assert/strict'
import { spawn, type ChildProcess } from 'node:child_process'
import { mkdtemp, rm, writeFile, mkdir } from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import pg from 'pg'

const BASE_DATABASE = process.env.LOOMIA_TEST_DATABASE ?? process.env.DATABASE_URL ?? ''
const SERVER_PORT = Number(process.env.LOOMIA_TEST_PORT ?? 8098)
const DB_NAME = `loomia_admin_${process.pid}`
const BASE_URL = `http://127.0.0.1:${SERVER_PORT}`
const ADMIN_EMAIL = 'root@loomia.dev'
const ADMIN_PASSWORD = 'senha-forte-1'

let adminUrl = ''
let testUrl = ''
let server: ChildProcess | null = null
let workDir = ''

function withDatabase(url: string, database: string): string {
  const parsed = new URL(url)
  parsed.pathname = `/${database}`
  return parsed.toString()
}

async function databaseAvailable(): Promise<boolean> {
  if (!BASE_DATABASE) return false
  try {
    const client = new pg.Client({ connectionString: withDatabase(BASE_DATABASE, 'postgres') })
    await client.connect()
    await client.end()
    return true
  } catch {
    return false
  }
}

const available = await databaseAvailable()

before(async () => {
  if (!available) return
  adminUrl = withDatabase(BASE_DATABASE, 'postgres')
  testUrl = withDatabase(BASE_DATABASE, DB_NAME)

  const admin = new pg.Client({ connectionString: adminUrl })
  await admin.connect()
  await admin.query(`DROP DATABASE IF EXISTS ${DB_NAME}`)
  await admin.query(`CREATE DATABASE ${DB_NAME}`)
  await admin.end()

  workDir = await mkdtemp(path.join(os.tmpdir(), 'loomia-admin-'))
  await mkdir(path.join(workDir, 'worlds'), { recursive: true })
  await writeFile(
    path.join(workDir, 'worlds', 'testflat.loom'),
    JSON.stringify({
      id: 'testflat',
      name: 'Test Flat',
      spawn: [0, 2, 0],
      sky_color: [150, 200, 240],
      blocks: [{ position: [0, -0.5, 0], size: [40, 1, 40], color: [100, 160, 60], material: 'grass' }],
    }),
  )
  await mkdir(path.join(workDir, 'site'), { recursive: true })
  await writeFile(path.join(workDir, 'site', 'index.html'), '<!doctype html><title>Loomia</title>')

  server = spawn(process.execPath, [path.resolve('dist/src/server.js')], {
    env: {
      ...process.env,
      DATABASE_URL: testUrl,
      LOOMIA_PORT: String(SERVER_PORT),
      LOOMIA_BIND: '127.0.0.1',
      LOOMIA_WORLDS_DIR: path.join(workDir, 'worlds'),
      LOOMIA_FRONTEND_DIR: path.join(workDir, 'site'),
      LOOMIA_ROOM_SECRET: 'test-secret',
      LOOMIA_ADMIN_EMAILS: ADMIN_EMAIL,
      LOOMIA_ADMIN_ORIGINS: '',
      LOOMIA_LIMIT_REGISTER: '500',
      LOOMIA_LIMIT_LOGIN: '500',
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  })
  server.stdout?.on('data', (data) => process.env.LOOMIA_TEST_VERBOSE && console.log('[server]', String(data).trim()))
  server.stderr?.on('data', (data) => process.env.LOOMIA_TEST_VERBOSE && console.error('[server]', String(data).trim()))

  for (let attempt = 0; attempt < 120; attempt++) {
    try {
      if ((await fetch(`${BASE_URL}/api/health`)).ok) return
    } catch {
      /* ainda subindo */
    }
    await new Promise((resolve) => setTimeout(resolve, 100))
  }
  throw new Error('o servidor não subiu')
})

after(async () => {
  server?.kill('SIGTERM')
  await new Promise((resolve) => setTimeout(resolve, 300))
  if (available) {
    const admin = new pg.Client({ connectionString: adminUrl })
    await admin.connect()
    await admin.query(`DROP DATABASE IF EXISTS ${DB_NAME} WITH (FORCE)`)
    await admin.end()
  }
  if (workDir) await rm(workDir, { recursive: true, force: true })
})

const skip = available ? false : 'defina LOOMIA_TEST_DATABASE/DATABASE_URL para um PostgreSQL acessível'

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

interface Reply {
  status: number
  body: any
  cookie: string | null
}

async function call(
  method: string,
  route: string,
  options: { body?: unknown; cookie?: string | null } = {},
): Promise<Reply> {
  const headers: Record<string, string> = { 'content-type': 'application/json' }
  if (options.cookie) headers.cookie = options.cookie
  const response = await fetch(`${BASE_URL}${route}`, {
    method,
    headers,
    body: options.body === undefined ? undefined : JSON.stringify(options.body),
  })
  const text = await response.text()
  return {
    status: response.status,
    body: text ? JSON.parse(text) : {},
    cookie: response.headers.get('set-cookie')?.split(';')[0] ?? null,
  }
}

const unique = (): string => Math.random().toString(36).slice(2, 8)

async function register(): Promise<Reply & { id: string; nickname: string; email: string }> {
  const handle = unique()
  const payload = {
    firstName: 'Teste',
    lastName: 'Silva',
    nickname: `user_${handle}`,
    email: `user_${handle}@loomia.dev`,
    password: 'senha-forte-1',
    passwordConfirm: 'senha-forte-1',
  }
  const reply = await call('POST', '/api/auth/register', { body: payload })
  assert.equal(reply.status, 201, JSON.stringify(reply.body))
  return { ...reply, id: reply.body.user.id, nickname: payload.nickname, email: payload.email }
}

/** Cria (uma vez) e entra como admin, via LOOMIA_ADMIN_EMAILS. */
async function adminSession(): Promise<string> {
  await call('POST', '/api/auth/register', {
    body: {
      firstName: 'Root',
      lastName: 'Loomia',
      nickname: `root_${unique()}`,
      email: ADMIN_EMAIL,
      password: ADMIN_PASSWORD,
      passwordConfirm: ADMIN_PASSWORD,
    },
  })
  const login = await call('POST', '/api/auth/login', {
    body: { login: ADMIN_EMAIL, password: ADMIN_PASSWORD },
  })
  assert.equal(login.status, 200, JSON.stringify(login.body))
  return login.cookie!
}

// ---------------------------------------------------------------------------
// Testes
// ---------------------------------------------------------------------------

test('painel admin recusa jogador comum', { skip }, async () => {
  const player = await register()
  assert.equal((await call('GET', '/api/admin/dashboard', { cookie: player.cookie })).status, 403)
  assert.equal((await call('GET', '/api/admin/players', { cookie: player.cookie })).status, 403)
  assert.equal((await call('GET', '/api/admin/dashboard')).status, 401)
})

test('admin por email: dashboard com os 4 relatórios', { skip }, async () => {
  const cookie = await adminSession()
  await register()
  await register()

  const response = await call('GET', '/api/admin/dashboard', { cookie })
  assert.equal(response.status, 200)
  const stats = response.body
  assert.ok(stats.users.online + stats.users.offline >= 2)
  assert.ok(stats.registrations.total >= 3)
  assert.ok(stats.registrations.series.length >= 14)
  assert.ok(Array.isArray(stats.moderation.series))
  assert.equal(typeof stats.maps.published, 'number')
  assert.equal(typeof stats.reports.open, 'number')
  assert.equal(typeof stats.tickets.open, 'number')
})

test('busca de jogador mostra dados básicos e contagem de mapas', { skip }, async () => {
  const cookie = await adminSession()
  const target = await register()

  const published = await call('POST', '/api/corner/maps', {
    cookie: target.cookie,
    body: {
      name: 'Mundo do Alvo',
      description: 'teste',
      visible: true,
      world: {
        id: `alvo-${unique()}`,
        name: 'Mundo do Alvo',
        spawn: [0, 1, 0],
        sky_color: [0, 0, 0],
        blocks: [{ position: [0, -1, 0], size: [10, 1, 10], color: [1, 2, 3], material: 'grass' }],
      },
    },
  })
  assert.equal(published.status, 201)

  const found = await call('GET', `/api/admin/players?q=${encodeURIComponent(target.nickname)}`, { cookie })
  const player = found.body.players.find((entry: { id: string }) => entry.id === target.id)
  assert.ok(player, 'jogador precisa aparecer na busca')
  assert.equal(player.email, target.email.toLowerCase())
  assert.equal(player.maps, 1)
  assert.ok(player.createdAt)

  const details = await call('GET', `/api/admin/players/${target.id}`, { cookie })
  assert.equal(details.status, 200)
  assert.equal(details.body.player.id, target.id)
})

test('excluir conta libera email e apelido', { skip }, async () => {
  const cookie = await adminSession()
  const target = await register()

  assert.equal((await call('DELETE', `/api/admin/players/${target.id}`, { cookie })).status, 200)
  assert.equal((await call('GET', `/api/admin/players/${target.id}`, { cookie })).status, 404)
  assert.equal((await call('GET', '/api/me', { cookie: target.cookie })).status, 401)

  const again = await call('POST', '/api/auth/register', {
    body: {
      firstName: 'Teste',
      lastName: 'Silva',
      nickname: target.nickname,
      email: target.email,
      password: 'senha-forte-1',
      passwordConfirm: 'senha-forte-1',
    },
  })
  assert.equal(again.status, 201, JSON.stringify(again.body))
})

test('banir bloqueia email/login e libera o apelido', { skip }, async () => {
  const cookie = await adminSession()
  const target = await register()

  const banned = await call('POST', `/api/admin/players/${target.id}/ban`, {
    cookie,
    body: { reason: 'trapaça no mundo de teste' },
  })
  assert.equal(banned.status, 200)

  assert.equal((await call('GET', '/api/me', { cookie: target.cookie })).status, 401)

  const login = await call('POST', '/api/auth/login', {
    body: { login: target.email, password: 'senha-forte-1' },
  })
  assert.equal(login.status, 403)
  assert.equal(login.body.code, 'account_blocked')

  const sameEmail = await call('POST', '/api/auth/register', {
    body: {
      firstName: 'Outro',
      lastName: 'Nome',
      nickname: `outro_${unique()}`,
      email: target.email,
      password: 'senha-forte-1',
      passwordConfirm: 'senha-forte-1',
    },
  })
  assert.equal(sameEmail.status, 403)
  assert.equal(sameEmail.body.code, 'email_banned')

  const sameNick = await call('POST', '/api/auth/register', {
    body: {
      firstName: 'Outro',
      lastName: 'Nome',
      nickname: target.nickname,
      email: `novo_${unique()}@loomia.dev`,
      password: 'senha-forte-1',
      passwordConfirm: 'senha-forte-1',
    },
  })
  assert.equal(sameNick.status, 201, JSON.stringify(sameNick.body))

  assert.equal((await call('POST', `/api/admin/players/${target.id}/unban`, { cookie })).status, 200)
  const access = await call('POST', '/api/auth/login', {
    body: { login: target.email, password: 'senha-forte-1' },
  })
  assert.equal(access.status, 200)
})

test('reports: jogador reporta, admin resolve', { skip }, async () => {
  const cookie = await adminSession()
  const reporter = await register()
  const target = await register()

  const created = await call('POST', '/api/reports', {
    cookie: reporter.cookie,
    body: { targetId: target.id, description: 'Ele quebrou as regras do mundo de teste' },
  })
  assert.equal(created.status, 201)
  assert.ok(created.body.report.targetName.length > 0)
  assert.ok(created.body.report.reporterName.length > 0)

  const list = await call('GET', '/api/admin/reports?status=open', { cookie })
  const report = list.body.reports.find((entry: { id: string }) => entry.id === created.body.report.id)
  assert.ok(report)

  const resolved = await call('PUT', `/api/admin/reports/${report.id}`, {
    cookie,
    body: { status: 'resolved', resolution: 'advertido' },
  })
  assert.equal(resolved.status, 200)
  assert.equal(resolved.body.report.status, 'resolved')

  const forbidden = await call('PUT', `/api/admin/reports/${report.id}`, {
    cookie: reporter.cookie,
    body: { status: 'dismissed' },
  })
  assert.equal(forbidden.status, 403)
})

test('tickets: jogador abre, admin responde', { skip }, async () => {
  const cookie = await adminSession()
  const player = await register()

  const ticket = await call('POST', '/api/tickets', {
    cookie: player.cookie,
    body: { category: 'suggestion', subject: 'Ideia: modo espectador', body: 'Seria legal assistir aos mundos.' },
  })
  assert.equal(ticket.status, 201)

  const mine = await call('GET', '/api/me/tickets', { cookie: player.cookie })
  assert.equal(mine.body.tickets.length, 1)

  const answered = await call('PUT', `/api/admin/tickets/${ticket.body.ticket.id}`, {
    cookie,
    body: { status: 'resolved', resolution: 'vamos avaliar' },
  })
  assert.equal(answered.status, 200)
  assert.equal(answered.body.ticket.status, 'resolved')
})

test('social: pedido com mensagem, aceite, conversa, favorito e perfil', { skip }, async () => {
  const ana = await register()
  const bob = await register()

  const request = await call('POST', '/api/friends/requests', {
    cookie: ana.cookie,
    body: { targetId: bob.id, message: 'Oi! Joguei no seu mundo, quer jogar junto?' },
  })
  assert.equal(request.status, 201)
  assert.ok(request.body.request.message.includes('Joguei'))

  const tooEarly = await call('POST', `/api/messages/${bob.id}`, {
    cookie: ana.cookie,
    body: { body: 'oi' },
  })
  assert.equal(tooEarly.status, 403)

  const bobView = await call('GET', '/api/me/friends', { cookie: bob.cookie })
  const incoming = bobView.body.requests.find((entry: { id: string }) => entry.id === request.body.request.id)
  assert.equal(incoming.direction, 'incoming')
  assert.ok(incoming.message.includes('Joguei'))

  const accepted = await call('POST', `/api/friends/requests/${request.body.request.id}/accept`, { cookie: bob.cookie })
  assert.equal(accepted.status, 200)
  assert.equal(accepted.body.request.status, 'accepted')

  const sent = await call('POST', `/api/messages/${bob.id}`, {
    cookie: ana.cookie,
    body: { body: 'valeu!' },
  })
  assert.equal(sent.status, 201)
  const thread = await call('GET', `/api/messages/${ana.id}`, { cookie: bob.cookie })
  assert.equal(thread.body.messages.length, 1)

  assert.equal((await call('POST', `/api/players/${bob.id}/favorite`, { cookie: ana.cookie })).status, 200)
  const favorites = await call('GET', '/api/me/favorites', { cookie: ana.cookie })
  assert.equal(favorites.body.favorites[0].id, bob.id)

  const profile = await call('GET', `/api/players/${bob.id}`, { cookie: ana.cookie })
  assert.equal(profile.status, 200)
  assert.equal(profile.body.isFriend, true)
  assert.equal(profile.body.isFavorite, true)
  assert.equal(profile.body.isMe, false)

  const mine = await call('GET', `/api/players/${ana.id}`, { cookie: ana.cookie })
  assert.equal(mine.body.isMe, true)
})

test('bio e busca de jogadores', { skip }, async () => {
  const ana = await register()
  const bob = await register()

  const bio = await call('PUT', '/api/me/bio', {
    cookie: bob.cookie,
    body: { bio: 'Construtor de mundos' },
  })
  assert.equal(bio.status, 200)

  const found = await call('GET', `/api/players?q=${encodeURIComponent(bob.nickname)}`, { cookie: ana.cookie })
  assert.ok(found.body.players.some((entry: { id: string }) => entry.id === bob.id))

  const profile = await call('GET', `/api/players/${bob.id}`, { cookie: ana.cookie })
  assert.equal(profile.body.bio, 'Construtor de mundos')
})
