// Alocação das salas: escolhe porta livre, sobe o processo Go (`headless`),
// entrega o token de entrada para o cliente e encerra salas ociosas.
//
// Cada sala é um processo `loomia-gameserver` com o seu próprio mapa carregado:
//   -map data/worlds/<id>.loom -port <livre> -token <secreto> -secret <...>
//
// O cliente recebe apenas `host`, `port` e `token` através do link
// `loomia://launch?server=host:port&token=...&map=...`.

import { spawn } from 'node:child_process'
import dgram from 'node:dgram'
import fs from 'node:fs'
import net from 'node:net'
import path from 'node:path'
import { randomToken, now } from './util.js'

function tcpFree(port) {
  return new Promise((resolve) => {
    const server = net.createServer()
    server.once('error', () => resolve(false))
    server.once('listening', () => server.close(() => resolve(true)))
    server.listen(port)
  })
}

function udpFree(port) {
  return new Promise((resolve) => {
    const socket = dgram.createSocket('udp4')
    socket.once('error', () => resolve(false))
    socket.once('listening', () => socket.close(() => resolve(true)))
    socket.bind(port)
  })
}

/** A sala Go escuta UDP (TCP e UDP usam tabelas separadas). */
async function portFree(port) {
  if (!(await tcpFree(port))) return false
  return udpFree(port)
}

export class RoomManager {
  constructor({ config, store, log }) {
    this.config = config
    this.store = store
    this.log = log
    this.rooms = new Map() // id do mapa -> sala
    this.committing = new Map() // id do mapa -> Promise (evita corrida)
    this.reservedPorts = new Set() // portas escolhidas mas ainda abrindo
    this.nextSession = 1
    this.origin = defaultOrigin(config)
    setInterval(() => this.reap(), 5000).unref()
  }

  publicRoom(room) {
    return {
      session: room.session,
      map_id: room.mapId,
      map_name: room.mapName,
      host: room.host,
      port: room.port,
      players: room.players,
      capacity: room.capacity,
      started_at: new Date(room.startedAt).toISOString(),
      launch_token: room.launchToken,
      launch_url: launchUrl(room, { origin: this.origin }),
    }
  }

  find(mapId) {
    return this.rooms.get(mapId) ?? null
  }

  list() {
    return [...this.rooms.values()].map((room) => this.publicRoom(room))
  }

  /** Porta livre dentro do intervalo reservado. */
  async pickPort() {
    const { gameServerPortMin: min, gameServerPortMax: max } = this.config
    const used = new Set([...this.rooms.values()].map((room) => room.port))
    for (const port of this.reservedPorts) used.add(port)
    for (let port = min; port <= max; port++) {
      if (used.has(port)) continue
      // TCP no modo de diagnóstico; UDP é o transporte do jogo.
      if (this.config.tcpMode ? await tcpFree(port) : await portFree(port)) return port
    }
    return null
  }

  /** Obtém (ou cria) a sala do mapa e devolve o token de entrada. */
  async allocate(mapId, requestedName) {
    const current = this.rooms.get(mapId)
    if (current) {
      current.launchToken = randomToken(16)
      current.tokenIssuedAt = now()
      current.lastActivity = now()
      return this.publicRoom(current)
    }
    if (this.committing.has(mapId)) {
      return this.committing.get(mapId)
    }
    const pending = this.spawnRoom(mapId, requestedName).finally(() => this.committing.delete(mapId))
    this.committing.set(mapId, pending)
    return pending
  }

  executable() {
    // Prefere o binário; sem ele, `go run .` (útil em dev, primeiro uso lento).
    if (fs.existsSync(this.config.gameServerBin)) {
      return { command: this.config.gameServerBin, args: [] }
    }
    return { command: this.config.gameServerGo, args: ['run', '.'] }
  }

  async spawnRoom(mapId, requestedName) {
    const world = await this.store.readWorld(mapId)
    if (!world) throw new Error('mapa não encontrado')

    if (this.rooms.size >= this.config.maxRooms) {
      throw new Error('todas as salas estão lotadas, tente daqui a pouco')
    }
    const port = await this.pickPort()
    if (port === null) throw new Error('sem portas livres para novas salas')

    const session = this.nextSession++
    // Reserva a porta ANTES do await: duas alocações simultâneas para mapas
    // diferentes não podem escolher o mesmo número.
    this.reservedPorts.add(port)
    const launchToken = randomToken(16)
    const mapPath = this.store.pathOf(mapId)
    const { command, args } = this.executable()
    const inGoDir = command === this.config.gameServerGo

    const child = spawn(
      command,
      [
        ...args,
        '-map', mapPath,
        '-port', String(port),
        '-bind', this.config.gameServerBind,
        '-token', launchToken,
        '-secret', this.config.roomSecret,
        '-master', `http://${this.config.host}:${this.config.port}`,
        '-session', String(session),
        // O jogo escuta UDP; `LOOMIA_TCP=1` liga o modo TCP de diagnóstico.
        ...(this.config.tcpMode ? ['-tcp'] : []),
      ],
      {
        cwd: inGoDir ? this.config.gameServerGoDir : path.dirname(command),
        // stdout vira pipe (não pode ser null); o log do Go vai para stderr.
        stdio: ['ignore', 'pipe', 'pipe'],
        // O `-bind` é para o próprio jogo escutar; o host público é outro.
        env: { ...process.env, GOTOOLCHAIN: process.env.GOTOOLCHAIN ?? 'auto', PATH: process.env.PATH },
      },
    )
    child.on('error', (error) => this.log.error(`sala ${session}: ${error.message}`))

    const room = {
      session,
      mapId,
      mapName: requestedName ?? world.name ?? mapId,
      host: this.config.gameServerPublicHost,
      port,
      players: [],
      playerNames: [],
      capacity: this.store.findMap(mapId)?.maxPlayers ?? 32,
      startedAt: now(),
      lastActivity: now(),
      launchToken,
      tokenIssuedAt: now(),
      chat: [],
      child,
      settled: false,
    }

    room.ready = new Promise((resolve, reject) => {
      // O `go run` compila antes de executar: pode levar bem mais que o
      // binário pronto (a primeira vez baixa até o toolchain).
      const timeoutMs = inGoDir ? 180_000 : 30_000
      const timer = setTimeout(() => reject(new Error('a sala demorou demais para abrir')), timeoutMs)
      // O log do Go é escrito ANTES do bind da porta: esperar só por ele
      // fazia o cliente se conectar cedo demais. Agora a sala só fica
      // "pronta" quando a porta de fato aceita conexão.
      const started = now()
      const probe = () => {
        if (now() - started > timeoutMs) {
          clearTimeout(timer)
          reject(new Error('a sala demorou demais para abrir'))
          return
        }
        const socket = net.connect({ host: this.config.gameServerBind, port })
        socket.once('error', () => {
          socket.destroy()
          setTimeout(probe, 100)
        })
        socket.once('connect', () => {
          socket.destroy()
          clearTimeout(timer)
          room.settled = true
          resolve()
        })
        socket.setTimeout(1500, () => socket.destroy())
      }
      probe()
      child.once('exit', (code) => {
        clearTimeout(timer)
        this.rooms.delete(mapId)
        this.reservedPorts.delete(port)
        if (!room.settled) {
          room.settled = true
          reject(new Error(`a sala terminou antes de abrir (código ${code})`))
        }
      })
    })

    child.on('exit', (code) => this.log.info(`sala ${session} (${mapId}) encerrada (código ${code})`))
    this.rooms.set(mapId, room)
    try {
      await room.ready
    } finally {
      this.reservedPorts.delete(port)
    }
    this.log.info(`sala ${session} de "${mapId}" no ar em ${room.host}:${port}`)
      return this.publicRoom(room)
    }

    /** Valida o token de uma tentativa de entrada (usado em /api/rooms/:id). */
  validateJoin(mapId, token) {
    const room = this.rooms.get(mapId)
    if (!room || token !== room.launchToken) return null
    if (now() - room.tokenIssuedAt > this.config.launchTtlMs) return null
    return room
  }

  report({ session, port, players, chat }) {
    const room = [...this.rooms.values()].find((entry) => entry.session === session && entry.port === port)
    if (!room) return null
    room.players = Array.isArray(players) ? players.length : 0
    room.playerNames = Array.isArray(players) ? players.slice(0, 64) : []
    room.lastActivity = now()
    if (chat && chat.text) {
      room.chat.push(chat)
      if (room.chat.length > 20) room.chat.shift()
    }
    return room
  }

  remove(session) {
    for (const [mapId, room] of this.rooms) {
      if (room.session === session) {
        this.rooms.delete(mapId)
        room.child.kill('SIGTERM')
      }
    }
  }

  async close(session) {
    this.remove(session)
  }

  /** Chamado na inicialização (o mapas já estão em disco). */
  async bootstrap() {
    return this
  }

  /** Encerra salas vazias há muito tempo. */
  reap() {
    const cutoff = now() - this.config.roomIdleMs
    for (const [mapId, room] of this.rooms) {
      if (room.players === 0 && room.lastActivity < cutoff) {
        this.log.info(`sala ${room.session} (${mapId}) ociosa: encerrando`)
        this.rooms.delete(mapId)
        room.child.kill('SIGTERM')
      }
    }
  }

  shutdown() {
    for (const room of this.rooms.values()) {
      room.child.kill('SIGTERM')
    }
    this.rooms.clear()
  }
}

/** Monta o link customizado que abre o cliente LOVR. */
export function launchUrl(room, { playerName, color, origin } = {}) {
  const query = new URLSearchParams({
    server: `${room.host}:${room.port}`,
    token: room.launchToken,
    map: room.mapId,
    name: playerName ?? 'Loomian',
    color: color ?? 'eabe19',
  })
  if (origin) query.set('origin', origin)
  return `loomia://launch?${query.toString()}`
}

export function defaultOrigin(config) {
  const port = config.port === 80 ? '' : `:${config.port}`
  return `http://${config.host}${port}/`
}
