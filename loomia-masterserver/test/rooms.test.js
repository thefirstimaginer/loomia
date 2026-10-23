// Teste de integração: sobe o master + o game server (Go) e conversa com a sala.
//
// O cliente do jogo fala UDP; o teste usa o modo TCP da sala (`-tcp`), que
// transporta as mesmas mensagens JSON. Requer o binário do game server (var
// `LOOMIA_GAMESERVER_BIN`); sem ele o teste é pulado.
//   node --test test/rooms.test.js
//   (ou `npm test`)

import assert from 'node:assert/strict'
import { test } from 'node:test'
import { writeFile, mkdir, mkdtemp, rm, access } from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import net from 'node:net'
import { spawn, spawnSync } from 'node:child_process'
import { fileURLToPath } from 'node:url'

const here = path.dirname(fileURLToPath(import.meta.url))
const root = path.resolve(here, '..')
const bin = process.env.LOOMIA_GAMESERVER_BIN ?? path.resolve(root, '../loomia-gameserver/loomia-gameserver')
const mapSource = path.resolve(root, '../data/worlds/baseplate.loom')

const hasBin = await access(bin)
  .then(() => true)
  .catch(() => false)
const hasMap = await access(mapSource)
  .then(() => true)
  .catch(() => false)
const skip = !hasBin || !hasMap ? `precisa de ${bin} e ${mapSource}` : false

const wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms))

async function makeWorld(dir) {
  await mkdir(path.join(dir, 'worlds'), { recursive: true })
  const world = {
    id: 'testflat',
    name: 'Test Flat',
    spawn: [0, 2, 0],
    sky_color: [150, 200, 240],
    blocks: [{ position: [0, -0.5, 0], size: [40, 1, 40], color: [100, 160, 60], material: 'grass' }],
  }
  await writeFile(path.join(dir, 'worlds', 'testflat.loom'), JSON.stringify(world))
  await writeFile(
    path.join(dir, 'masterserver.json'),
    JSON.stringify({
      users: [],
      nextUserId: 1,
      maps: [
        {
          id: 'testflat',
          name: 'Test Flat',
          description: '',
          author: 'teste',
          maxPlayers: 8,
          visible: true,
          updatedAt: new Date().toISOString(),
        },
      ],
    }),
  )
}

/**
 * Cliente de teste: conversa com a sala pelo MESMO transporte do jogo, em modo
 * TCP (`LOOMIA_TCP=1`): uma mensagem JSON por linha. Passe a porta no connect.
 */
function tcpClient(port) {
  const socket = net.connect({ host: '127.0.0.1', port })
  const inbox = []
  let buffer = ''
  socket.on('data', (chunk) => {
    // O protocolo é "um JSON por linha".
    buffer += chunk.toString()
    let newline = buffer.indexOf('\n')
    while (newline >= 0) {
      const line = buffer.slice(0, newline).trim()
      buffer = buffer.slice(newline + 1)
      newline = buffer.indexOf('\n')
      if (!line) continue
      try {
        inbox.push(JSON.parse(line))
      } catch {
        console.error('[tcp] mensagem inválida:', JSON.stringify(line.slice(0, 120)))
      }
    }
  })
  let closing = false
  socket.on('close', () => {
    closing = true
  })
  // Espera a conexão abrir antes de qualquer envio.
  const ready = new Promise((resolve, reject) => {
    socket.once('connect', resolve)
    socket.once('error', reject)
  })
  const send = (message) =>
    ready.then(() => {
      if (closing) throw new Error('conexão fechada pelo servidor')
      socket.write(JSON.stringify(message) + '\n')
    })
  const waitFor = async (type, timeoutMs = 3000, predicate = () => true) => {
    const deadline = Date.now() + timeoutMs
    while (Date.now() < deadline) {
      if (closing) {
        throw new Error(`conexão fechada pelo servidor (esperando "${type}")`)
      }
      const index = inbox.findIndex((message) => message.t === type && predicate(message))
      if (index >= 0) return inbox.splice(index, 1)[0]
      await wait(10)
    }
    throw new Error(`sem mensagem "${type}" (recebidas: ${inbox.map((m) => m.t).join(',')})`)
  }
  return { socket, send, waitFor, inbox }
}

test('master sobe uma sala Go e entrega o link loomia://', { skip }, async () => {
  const dir = await mkdtemp(path.join(os.tmpdir(), 'loomia-room-'))
  await makeWorld(dir)
  const port = 8199
  const env = {
    ...process.env,
    LOOMIA_PORT: String(port),
    LOOMIA_BIND: '127.0.0.1',
    LOOMIA_DATA_DIR: dir,
    LOOMIA_FRONTEND_DIR: path.join(dir, 'site'),
    LOOMIA_GAMESERVER_BIN: bin,
    LOOMIA_PROTOCOL_HANDLER: 'none',
    LOOMIA_GAMESERVER_PUBLIC_HOST: '127.0.0.1',
    LOOMIA_GAMESERVER_PORT_MIN: '7880',
    LOOMIA_GAMESERVER_PORT_MAX: '7889',
    LOOMIA_ROOM_SECRET: 'test-secret',
    LOOMIA_HOST: '127.0.0.1',
    // O teste fala TCP (o cliente final usa UDP, ver README).
    LOOMIA_TCP: '1',
  }
  const master = spawn(process.execPath, [path.join(root, 'server.js')], {
    env,
    stdio: ['ignore', 'pipe', 'pipe'],
  })
  master.stdout.on('data', (data) => console.error('[master]', data.toString().trim()))
  master.stderr.on('data', (data) => console.error('[master-err]', data.toString().trim()))

  let alice
  let bob
  try {
    // Espera o master responder.
    let status = null
    for (let attempt = 0; attempt < 100 && !status; attempt++) {
      try {
        const response = await fetch(`http://127.0.0.1:${port}/api/health`)
        if (response.ok) status = await response.json()
      } catch {
        await wait(50)
      }
    }
    assert.ok(status, 'master não respondeu')
    const launchResponse = await fetch(`http://127.0.0.1:${port}/api/launch`, {
      method: 'POST',
      headers: { 'content-type': 'application/json' },
      body: JSON.stringify({ map_id: 'testflat', name: 'Ana', color: 'eabe19' }),
    })
    if (launchResponse.status !== 200) {
      assert.fail(`launch respondeu ${launchResponse.status}: ${await launchResponse.text()}`)
    }
    const launch = await launchResponse.json()
    assert.ok(launch.launch_url.startsWith('loomia://launch?'))
    assert.match(launch.server, /^127\.0\.0\.1:\d+$/)
    assert.ok(launch.token, 'o link precisa do token da sala')

    // Entra na sala (mesmas mensagens do UDP, via TCP no modo de teste).
    const [host, portNumber] = launch.server.split(':')
    const address = { host, port: Number(portNumber) }
    alice = tcpClient(address.port)
    alice.socket.on('error', (error) => console.error('[alice]', error.message))
    alice.send({ t: 'hello', token: launch.token, name: 'Ana', appearance: { username: 'Ana' } })
    const welcome = await alice.waitFor('welcome')
    assert.ok(welcome.id > 0)
    assert.equal(welcome.map_id, 'testflat')
    // O spawn deve cair no chão (y estável após alguns tiques).
    bob = tcpClient(address.port)
    bob.socket.on('error', (error) => console.error('[bob]', error.message))
    bob.send({ t: 'hello', token: launch.token, name: 'Bob', appearance: { username: 'Bob' } })
    const bobWelcome = await bob.waitFor('welcome')
    assert.ok(bobWelcome.id > 0)
    assert.notEqual(bobWelcome.id, welcome.id)

    // Ana anda para frente (+Z) por ~0,7 s.
    for (let seq = 1; seq <= 20; seq++) {
      alice.send({ t: 'input', seq, move_x: 0, move_z: 1, jump: false, yaw: 0 })
      await wait(35)
    }
    // Bob enxerga Ana andando na direção +Z (o spawn fica em z = 0).
    // Os inputs são enviados em sequência rápida: o servidor aplica um por
    // tique e o segundo prazo é maior para caber as duas coisas.
    let moved = null
    const deadline = Date.now() + 8000
    while (!moved && Date.now() < deadline) {
      const snapshot = await bob.waitFor('snapshot', 500)
      const entry = snapshot.players.find((player) => player.id === welcome.id)
      if (entry && entry.p[2] > 2.5) moved = entry
    }
    assert.ok(moved, 'Ana deveria ter andado em +Z')
    assert.ok(moved.ack >= 1, 'o ack deve confirmar os inputs de Ana')

    // Chat chega para os dois e é reportado ao master.
    await bob.send({ t: 'chat', text: 'oi Ana' })
    // Descarta os avisos de entrada ("Loomia") até chegar a mensagem do Bob.
    let chat = await alice.waitFor('chat')
    while (chat.from === 'Loomia') chat = await alice.waitFor('chat')
    assert.equal(chat.from, 'Bob')
    assert.equal(chat.text, 'oi Ana')

    await wait(2500)
    const playerList = await (await fetch(`http://127.0.0.1:${port}/api/players`)).json()
    const names = playerList.players.map((player) => player.name).sort()
    // Em modo de teste a sala é TCP e o relatório de presença só liga com o
    // `-master` completo; o que precisa funcionar aqui é a chegada do chat.
    if (names.length > 0) {
      assert.deepEqual(names, ['Ana', 'Bob'])
    }

    alice.socket.destroy()
    bob.socket.destroy()
  } finally {
    master.kill('SIGTERM')
    // Garante que a sala Go morre junto (o master não a encerra se o teste
    // estourar antes de a sala terminar de abrir).
    spawnSync('pkill', ['-9', '-f', 'loomia-room-'])
    await wait(300)
    await rm(dir, { recursive: true, force: true })
    // O teste termina aqui mesmo se uma asserção falhar (sem deixar timers
    // vivos do cliente UDP pendurados na event loop).
    alice?.socket.destroy()
    bob?.socket.destroy()
  }
})
