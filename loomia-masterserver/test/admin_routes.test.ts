// Rotas do painel admin: autorização, exclusão da própria conta, validação de
// status e o vai-e-vem de ban/unban.
//
// PostgreSQL real + servidor em subprocesso (o fluxo depende de cookie).
// O admin é criado direto no banco (mesma tabela/constraint), para não depender
// do captcha — que é justamente o que este arquivo não quer dirigir.

import { test, before, after } from 'node:test'
import assert from 'node:assert/strict'
import { spawn, type ChildProcess } from 'node:child_process'
import { mkdtemp, rm, writeFile, mkdir } from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import pg from 'pg'
import { hashPassword } from '../src/auth/password.js'

const BASE_DATABASE = process.env.LOOMIA_TEST_DATABASE ?? process.env.DATABASE_URL ?? ''
const SERVER_PORT = Number(process.env.LOOMIA_TEST_PORT ?? 8097)
const DB_NAME = `loomia_admin_routes_${process.pid}`
const BASE_URL = `http://127.0.0.1:${SERVER_PORT}`
const ADMIN_EMAIL = 'guardian@loomia.dev'
const ADMIN_PASSWORD = 'senha-forte-1'
const FORBIDDEN_ORIGIN = 'https://evil.example'

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

  workDir = await mkdtemp(path.join(os.tmpdir(), 'loomia-admin-routes-'))
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
  options: { body?: unknown; cookie?: string | null; origin?: string } = {},
): Promise<Reply> {
  const headers: Record<string, string> = { 'content-type': 'application/json' }
  if (options.cookie) headers.cookie = options.cookie
  if (options.origin) headers.origin = options.origin
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

/**
 * Cria a conta admin direto no banco (o registro por API exige captcha real) e
 * entra com ela pela rota normal de login.
 */
async function adminSession(): Promise<{ cookie: string; id: string }> {
  const client = new pg.Client({ connectionString: testUrl })
  await client.connect()
  try {
    const existing = await client.query('SELECT id FROM users WHERE lower(email) = lower($1)', [ADMIN_EMAIL])
    if (existing.rowCount === 0) {
      const passwordHash = await hashPassword(ADMIN_PASSWORD)
      await client.query(
        `INSERT INTO users (first_name, last_name, nickname, email, password_hash, role)
         VALUES ($1, $2, $3, $4, $5, 'admin')`,
        ['Guardian', 'Loomia', `guardian_${unique()}`, ADMIN_EMAIL, passwordHash],
      )
    }
  } finally {
    await client.end()
  }

  const login = await call('POST', '/api/auth/login', { body: { login: ADMIN_EMAIL, password: ADMIN_PASSWORD } })
  assert.equal(login.status, 200, JSON.stringify(login.body))
  assert.equal(login.body.user.role, 'admin')
  return { cookie: login.cookie!, id: login.body.user.id }
}

// ---------------------------------------------------------------------------
// Testes
// ---------------------------------------------------------------------------

test('rotas admin: anônimo recebe 401 e jogador comum 403', { skip }, async () => {
  const player = await register()
  for (const route of ['/api/admin/me', '/api/admin/dashboard', '/api/admin/players', '/api/admin/reports']) {
    assert.equal((await call('GET', route)).status, 401, route)
    assert.equal((await call('GET', route, { cookie: player.cookie })).status, 403, route)
  }
  // Escrita também é barrada antes de tocar no banco.
  assert.equal((await call('POST', `/api/admin/players/${player.id}/ban`, { cookie: player.cookie, body: {} })).status, 403)
})

test('rotas admin: origem desconhecida é recusada', { skip }, async () => {
  const admin = await adminSession()
  const blocked = await call('GET', '/api/admin/me', { cookie: admin.cookie, origin: FORBIDDEN_ORIGIN })
  assert.equal(blocked.status, 403)
  assert.equal(blocked.body.code, 'origin_forbidden')
})

test('admin não exclui a própria conta', { skip }, async () => {
  const admin = await adminSession()
  const me = await call('GET', '/api/admin/me', { cookie: admin.cookie })
  assert.equal(me.status, 200)
  assert.equal(me.body.admin.id, admin.id)

  const selfDelete = await call('DELETE', `/api/admin/players/${admin.id}`, { cookie: admin.cookie })
  assert.equal(selfDelete.status, 400)
  assert.equal(selfDelete.body.code, 'invalid')

  // A conta continua de pé.
  assert.equal((await call('GET', '/api/admin/me', { cookie: admin.cookie })).status, 200)
})

test('admin: ids e status inválidos são recusados', { skip }, async () => {
  const admin = await adminSession()
  const player = await register()

  const badPlayerId = await call('GET', '/api/admin/players/abc', { cookie: admin.cookie })
  assert.equal(badPlayerId.status, 404) // a rota só casa com \d+

  const badReportStatus = await call('PUT', '/api/admin/reports/1', {
    cookie: admin.cookie,
    body: { status: 'arquivado' },
  })
  assert.equal(badReportStatus.status, 400)
  assert.equal(badReportStatus.body.code, 'invalid')

  const badTicketStatus = await call('PUT', '/api/admin/tickets/1', {
    cookie: admin.cookie,
    body: { status: 'arquivado' },
  })
  assert.equal(badTicketStatus.status, 400)
  assert.equal(badTicketStatus.body.code, 'invalid')

  // Status válido mas id inexistente: 404 do banco, não 400.
  const missing = await call('PUT', '/api/admin/tickets/999999', {
    cookie: admin.cookie,
    body: { status: 'closed' },
  })
  assert.equal(missing.status, 404)
  assert.equal(missing.body.code, 'not_found')

  assert.equal((await call('GET', `/api/admin/players/${player.id}`, { cookie: admin.cookie })).status, 200)
})

test('ban/unban: banir derruba a sessão, desbanir devolve o acesso', { skip }, async () => {
  const admin = await adminSession()
  const target = await register()

  assert.equal((await call('GET', '/api/me', { cookie: target.cookie })).status, 200)

  const banned = await call('POST', `/api/admin/players/${target.id}/ban`, {
    cookie: admin.cookie,
    body: { reason: 'teste de ban' },
  })
  assert.equal(banned.status, 200, JSON.stringify(banned.body))
  assert.equal(banned.body.banned, true)
  assert.equal((await call('GET', '/api/me', { cookie: target.cookie })).status, 401)

  const details = await call('GET', `/api/admin/players/${target.id}`, { cookie: admin.cookie })
  assert.equal(details.status, 200)
  assert.equal(details.body.player.status, 'banned')
  assert.ok(details.body.player.bannedAt)

  const unbanned = await call('POST', `/api/admin/players/${target.id}/unban`, { cookie: admin.cookie })
  assert.equal(unbanned.status, 200, JSON.stringify(unbanned.body))
  assert.equal(unbanned.body.banned, false)

  const login = await call('POST', '/api/auth/login', {
    body: { login: target.email, password: 'senha-forte-1' },
  })
  assert.equal(login.status, 200)
  assert.ok(login.cookie)

  // Unban de id inexistente dá 404.
  const missing = await call('POST', '/api/admin/players/999999/unban', { cookie: admin.cookie })
  assert.equal(missing.status, 404)
  assert.equal(missing.body.code, 'not_found')
})

test('excluir jogador inexistente devolve 404', { skip }, async () => {
  const admin = await adminSession()
  const removed = await call('DELETE', '/api/admin/players/999999', { cookie: admin.cookie })
  assert.equal(removed.status, 404)
  assert.equal(removed.body.code, 'not_found')
})
