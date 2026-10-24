// Testes da API contra um PostgreSQL real.
//
//   DATABASE_URL=postgresql://loomia@127.0.0.1:55432/loomia_test npm test
//
// Sem `DATABASE_URL` (ou sem banco acessível) os testes são pulados.
// Cada execução usa um banco próprio (`loomia_test_<pid>`) criado/derrubado.

import { test, before, after } from 'node:test'
import assert from 'node:assert/strict'
import { spawn, type ChildProcess } from 'node:child_process'
import { mkdtemp, rm, writeFile, mkdir } from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import pg from 'pg'

const BASE_DATABASE = process.env.LOOMIA_TEST_DATABASE ?? process.env.DATABASE_URL ?? ''
const SERVER_PORT = Number(process.env.LOOMIA_TEST_PORT ?? 8099)
const DB_NAME = `loomia_test_${process.pid}`
const BASE_URL = `http://127.0.0.1:${SERVER_PORT}`
const HERO_ORIGIN = '127.0.0.1'

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
    const admin = withDatabase(BASE_DATABASE, 'postgres')
    const client = new pg.Client({ connectionString: admin })
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

  workDir = await mkdtemp(path.join(os.tmpdir(), 'loomia-api-'))
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
      LOOMIA_BIND: HERO_ORIGIN,
      LOOMIA_WORLDS_DIR: path.join(workDir, 'worlds'),
      LOOMIA_FRONTEND_DIR: path.join(workDir, 'site'),
      LOOMIA_ROOM_SECRET: 'test-secret',
    },
    stdio: ['ignore', 'pipe', 'pipe'],
  })
  server.stdout?.on('data', (data) => process.env.LOOMIA_TEST_VERBOSE && console.log('[server]', data.toString().trim()))
  server.stderr?.on('data', (data) => process.env.LOOMIA_TEST_VERBOSE && console.error('[server]', data.toString().trim()))

  // Espera o servidor responder.
  for (let attempt = 0; attempt < 120; attempt++) {
    try {
      const response = await fetch(`${BASE_URL}/api/health`)
      if (response.ok) return
    } catch {
      // ainda subindo
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

interface ResponseInfo {
  status: number
  body: any
  cookie: string | null
}

async function call(
  method: string,
  route: string,
  options: { body?: unknown; cookie?: string | null } = {},
): Promise<ResponseInfo> {
  const headers: Record<string, string> = { 'content-type': 'application/json' }
  if (options.cookie) headers.cookie = options.cookie
  const response = await fetch(`${BASE_URL}${route}`, {
    method,
    headers,
    body: options.body === undefined ? undefined : JSON.stringify(options.body),
  })
  const text = await response.text()
  const setCookie = response.headers.get('set-cookie')
  return {
    status: response.status,
    body: text ? JSON.parse(text) : {},
    cookie: setCookie ? setCookie.split(';')[0]! : null,
  }
}

const AVATAR = {
  name: 'Meu primeiro avatar',
  bodyColor: [234, 190, 25],
  shirtColor: [15, 120, 190],
  pantsColor: [35, 75, 45],
  hat: 'cap',
}

const unique = (): string => Math.random().toString(36).slice(2, 8)

// ---------------------------------------------------------------------------
// Testes
// ---------------------------------------------------------------------------

test('registro: cria conta, devolve cookie de sessão e manda para o onboarding', { skip }, async () => {
  const handle = unique()
  const response = await call('POST', '/api/auth/register', {
    body: {
      firstName: 'Ana',
      lastName: 'Lima',
      nickname: `ana_${handle}`,
      email: `ana_${handle}@loomia.dev`,
      password: 'senha-super-secreta',
      passwordConfirm: 'senha-super-secreta',
    },
  })
  assert.equal(response.status, 201)
  assert.equal(response.body.next, 'onboarding')
  assert.equal(response.body.user.nickname, `ana_${handle}`)
  assert.ok(response.cookie?.startsWith('loomia_session='))

  // A senha nunca volta na API.
  assert.equal(JSON.stringify(response.body).includes('senha-super-secreta'), false)
})

test('registro: senhas diferentes e email duplicado são rejeitados', { skip }, async () => {
  const handle = unique()
  const mismatch = await call('POST', '/api/auth/register', {
    body: {
      firstName: 'Ana',
      lastName: 'Lima',
      nickname: `x_${handle}`,
      email: `x_${handle}@loomia.dev`,
      password: 'senha-1234',
      passwordConfirm: 'outra-senha',
    },
  })
  assert.equal(mismatch.status, 400)
  assert.equal(mismatch.body.code, 'password_mismatch')

  const payload = {
    firstName: 'Bia',
    lastName: 'Souza',
    nickname: `bia_${handle}`,
    email: `bia_${handle}@loomia.dev`,
    password: 'senha-1234',
    passwordConfirm: 'senha-1234',
  }
  assert.equal((await call('POST', '/api/auth/register', { body: payload })).status, 201)

  const duplicate = await call('POST', '/api/auth/register', {
    body: { ...payload, nickname: `outra_${handle}` },
  })
  assert.equal(duplicate.status, 409)
  assert.equal(duplicate.body.code, 'email_taken')
})

test('login aceita email OU apelido e recusa senha errada', { skip }, async () => {
  const handle = unique()
  const nickname = `caio_${handle}`
  const email = `caio_${handle}@loomia.dev`
  await call('POST', '/api/auth/register', {
    body: {
      firstName: 'Caio',
      lastName: 'Reis',
      nickname,
      email,
      password: 'minha-senha-123',
      passwordConfirm: 'minha-senha-123',
    },
  })

  const byEmail = await call('POST', '/api/auth/login', { body: { login: email, password: 'minha-senha-123' } })
  assert.equal(byEmail.status, 200)
  assert.equal(byEmail.body.hasAvatar, false)

  const byNickname = await call('POST', '/api/auth/login', { body: { login: nickname, password: 'minha-senha-123' } })
  assert.equal(byNickname.status, 200)

  const wrong = await call('POST', '/api/auth/login', { body: { login: email, password: 'chutando' } })
  assert.equal(wrong.status, 401)
  assert.equal(wrong.body.code, 'invalid_credentials')

  const unknown = await call('POST', '/api/auth/login', { body: { login: `nada_${handle}`, password: 'x' } })
  assert.equal(unknown.status, 401)
})

test('rotas protegidas exigem login (mapas e corner)', { skip }, async () => {
  assert.equal((await call('POST', '/api/maps/testflat/launch')).status, 401)
  assert.equal((await call('POST', '/api/launch', { body: { mapId: 'testflat' } })).status, 401)
  assert.equal((await call('GET', '/api/corner/maps')).status, 401)
  // A listagem publica continua aberta para a landing.
  assert.equal((await call('GET', '/api/maps')).status, 200)
})


test('avatares: cria vários, ativa um, edita e remove', { skip }, async () => {
  const handle = unique()
  const registered = await call('POST', '/api/auth/register', {
    body: {
      firstName: 'Duda',
      lastName: 'Matos',
      nickname: `duda_${handle}`,
      email: `duda_${handle}@loomia.dev`,
      password: 'senha-forte-1',
      passwordConfirm: 'senha-forte-1',
    },
  })
  const cookie = registered.cookie!

  const first = await call('POST', '/api/me/avatars', { cookie, body: AVATAR })
  assert.equal(first.status, 201)
  assert.equal(first.body.avatar.isActive, true)

  const second = await call('POST', '/api/me/avatars', {
    cookie,
    body: { ...AVATAR, name: 'Avatar pirata', shirtColor: [190, 20, 40], hat: 'top_hat' },
  })
  assert.equal(second.status, 201)
  // O novo ativo desativa o anterior.
  assert.equal(second.body.avatar.isActive, true)

  let list = await call('GET', '/api/me/avatars', { cookie })
  assert.equal(list.body.avatars.length, 2)
  assert.equal(list.body.avatars.filter((avatar: { isActive: boolean }) => avatar.isActive).length, 1)

  const activated = await call('POST', `/api/me/avatars/${first.body.avatar.id}/activate`, { cookie })
  assert.equal(activated.status, 200)
  assert.equal(activated.body.avatar.isActive, true)

  const edited = await call('PUT', `/api/me/avatars/${first.body.avatar.id}`, {
    cookie,
    body: { ...AVATAR, name: 'Renomeado', bodyColor: [10, 20, 30] },
  })
  assert.equal(edited.status, 200)
  assert.equal(edited.body.avatar.name, 'Renomeado')
  assert.deepEqual(edited.body.avatar.bodyColor, [10, 20, 30])

  const removed = await call('DELETE', `/api/me/avatars/${second.body.avatar.id}`, { cookie })
  assert.equal(removed.status, 200)
  list = await call('GET', '/api/me/avatars', { cookie })
  assert.equal(list.body.avatars.length, 1)

  // Não dá para editar avatar de outra conta.
  const other = await call('POST', '/api/auth/register', {
    body: {
      firstName: 'Eva',
      lastName: 'Prado',
      nickname: `eva_${handle}`,
      email: `eva_${handle}@loomia.dev`,
      password: 'senha-forte-1',
      passwordConfirm: 'senha-forte-1',
    },
  })
  const foreign = await call('PUT', `/api/me/avatars/${first.body.avatar.id}`, {
    cookie: other.cookie,
    body: AVATAR,
  })
  assert.equal(foreign.status, 404)
})

test("Loomier's Corner: publica, lista, atualiza e só permite mudar o próprio mapa", { skip }, async () => {
  const handle = unique()
  const owner = await call('POST', '/api/auth/register', {
    body: {
      firstName: 'Fabi',
      lastName: 'Nunes',
      nickname: `fabi_${handle}`,
      email: `fabi_${handle}@loomia.dev`,
      password: 'senha-forte-2',
      passwordConfirm: 'senha-forte-2',
    },
  })
  const cookie = owner.cookie!

  const world = {
    id: `mapa-${handle}`,
    name: `Mapa ${handle}`,
    spawn: [0, 2, 0],
    sky_color: [150, 200, 240],
    blocks: [
      { position: [0, -0.5, 0], size: [30, 1, 30], color: [90, 160, 70], material: 'grass' },
      { position: [0, 3, 0], size: [2, 2, 2], color: [200, 60, 60], material: 'brick' },
    ],
  }

  const published = await call('POST', '/api/corner/maps', {
    cookie,
    body: { name: world.name, description: 'criado no teste', visible: true, maxPlayers: 16, world },
  })
  assert.equal(published.status, 201)
  assert.equal(published.body.map.id, world.id)
  assert.equal(published.body.map.blocks, 2)
  assert.equal(published.body.map.visibility, 'public')

  const mine = await call('GET', '/api/corner/maps', { cookie })
  assert.equal(mine.body.maps.length, 1)

  const explore = await call('GET', `/api/maps?q=${handle}`)
  assert.equal(explore.body.length, 1)
  assert.equal(explore.body[0].name, world.name)

  const updated = await call('PUT', `/api/corner/maps/${world.id}`, {
    cookie,
    body: { ...world, name: 'Nome novo', blocks: world.blocks.slice(0, 1) },
  })
  assert.equal(updated.status, 200)
  assert.equal(updated.body.map.name, 'Nome novo')
  assert.equal(updated.body.map.blocks, 1)

  const intruder = await call('POST', '/api/auth/register', {
    body: {
      firstName: 'Gil',
      lastName: 'Alves',
      nickname: `gil_${handle}`,
      email: `gil_${handle}@loomia.dev`,
      password: 'senha-forte-3',
      passwordConfirm: 'senha-forte-3',
    },
  })
  const forbidden = await call('PUT', `/api/corner/maps/${world.id}`, { cookie: intruder.cookie, body: world })
  assert.equal(forbidden.status, 403)

  // O mundo por HTTP é o que o cliente baixa.
  const publicWorld = await call('GET', `/api/maps/${world.id}`)
  assert.equal(publicWorld.status, 200)
  assert.equal(publicWorld.body.name, 'Nome novo')
  assert.ok(publicWorld.body.version)
})

test('logout invalida a sessão', { skip }, async () => {
  const handle = unique()
  const registered = await call('POST', '/api/auth/register', {
    body: {
      firstName: 'Hugo',
      lastName: 'Paz',
      nickname: `hugo_${handle}`,
      email: `hugo_${handle}@loomia.dev`,
      password: 'senha-forte-4',
      passwordConfirm: 'senha-forte-4',
    },
  })
  assert.equal((await call('GET', '/api/me', { cookie: registered.cookie })).status, 200)
  assert.equal((await call('POST', '/api/auth/logout', { cookie: registered.cookie })).status, 200)
  assert.equal((await call('GET', '/api/me', { cookie: registered.cookie })).status, 401)
})
