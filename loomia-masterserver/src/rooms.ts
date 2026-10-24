// Alocação das salas: escolhe porta livre, sobe o processo Go (`headless`),
// entrega o token de entrada para o cliente e encerra salas ociosas.

import { spawn, type ChildProcess } from 'node:child_process'
import dgram from 'node:dgram'
import fs from 'node:fs'
import net from 'node:net'
import path from 'node:path'
import { randomToken, now, type Logger } from './util.js'
import type { Config } from './config.js'

async function tcpFree(port: number): Promise<boolean> {
  return new Promise((resolve) => {
    const server = net.createServer()
    server.once('error', () => resolve(false))
    server.once('listening', () => server.close(() => resolve(true)))
    server.listen(port)
  })
}

async function udpFree(port: number): Promise<boolean> {
  return new Promise((resolve) => {
    const socket = dgram.createSocket('udp4')
    socket.once('error', () => resolve(false))
    socket.once('listening', () => socket.close(() => resolve(true)))
    socket.bind(port)
  })
}

async function portFree(port: number): Promise<boolean> {
  if (!(await tcpFree(port))) return false
  return udpFree(port)
}

export interface Room {
  session: number
  mapId: string
  mapName: string
  host: string
  port: number
  players: number
  playerNames: string[]
  capacity: number
  startedAt: number
  lastActivity: number
  launchToken: string
  tokenIssuedAt: number
  chat: { from: string; text: string }[]
  child: ChildProcess
  settled: boolean
}

export interface PublicRoom {
  session: number
  map_id: string
  map_name: string
  host: string
  port: number
  players: number
  capacity: number
  started_at: string
  launch_token: string
  launch_url: string
}

export class RoomManager {
  private rooms = new Map<string, Room>()
  private committing = new Map<string, Promise<PublicRoom>>()
  private reservedPorts = new Set<number>()
  private nextSession = 1
  private timer: NodeJS.Timeout

  constructor(
    private readonly config: Config,
    private readonly log: Logger,
  ) {
    this.timer = setInterval(() => this.reap(), 5000)
    this.timer.unref()
  }

  /** Sala aberta para um mapa (ou undefined). */
  find(mapId: string): Room | undefined {
    return this.rooms.get(mapId)
  }

  publicRoom(room: Room): PublicRoom {
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
      launch_url: launchUrl(room, { origin: this.origin() }),
    }
  }

  list(): PublicRoom[] {
    return [...this.rooms.values()].map((room) => this.publicRoom(room))
  }

  origin(): string {
    const port = this.config.port === 80 ? '' : `:${this.config.port}`
    return `http://${this.config.host}${port}/`
  }

  private async pickPort(): Promise<number | null> {
    const used = new Set([...this.rooms.values()].map((room) => room.port))
    for (const port of this.reservedPorts) used.add(port)
    for (let port = this.config.gameServerPortMin; port <= this.config.gameServerPortMax; port++) {
      if (used.has(port)) continue
      const free = this.config.tcpMode ? await tcpFree(port) : await portFree(port)
      if (free) return port
    }
    return null
  }

  /** Obtém (ou cria) a sala do mapa e devolve o token de entrada atualizado. */
  async allocate(mapId: string, requestedName?: string): Promise<PublicRoom> {
    const current = this.rooms.get(mapId)
    if (current) {
      current.launchToken = randomToken(16)
      current.tokenIssuedAt = now()
      current.lastActivity = now()
      return this.publicRoom(current)
    }
    const pending = this.committing.get(mapId)
    if (pending) return pending
    const promise = this.spawnRoom(mapId, requestedName).finally(() => this.committing.delete(mapId))
    this.committing.set(mapId, promise)
    return promise
  }

  private executable(): { command: string; args: string[] } {
    if (fs.existsSync(this.config.gameServerBin)) return { command: this.config.gameServerBin, args: [] }
    return { command: this.config.gameServerGo, args: ['run', '.'] }
  }

  private async spawnRoom(mapId: string, requestedName?: string): Promise<PublicRoom> {
    const mapPath = path.join(this.config.worldsDir, `${mapId}.loom`)
    if (!fs.existsSync(mapPath)) throw new Error('mapa não encontrado')

    if (this.rooms.size >= this.config.maxRooms) throw new Error('todas as salas estão lotadas, tente daqui a pouco')
    const port = await this.pickPort()
    if (port === null) throw new Error('sem portas livres para novas salas')

    const session = this.nextSession++
    this.reservedPorts.add(port)
    const launchToken = randomToken(16)
    const { command, args } = this.executable()
    const binary = fs.existsSync(this.config.gameServerBin)

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
        ...(this.config.tcpMode ? ['-tcp'] : []),
      ],
      {
        // `go run .` exige cwd no módulo; o binário roda de qualquer pasta.
        cwd: binary ? path.dirname(command) : this.config.gameServerGoDir,
        stdio: ['ignore', 'pipe', 'pipe'],
        env: { ...process.env, GOTOOLCHAIN: process.env.GOTOOLCHAIN ?? 'auto', PATH: process.env.PATH },
      },
    )
    child.on('error', (error) => this.log.error(`sala ${session}: ${error.message}`))
    // O log do processo é útil quando a sala não abre (porta ocupada, -h etc.).
    child.stdout?.on('data', (data: Buffer) => this.log.debug(`[sala ${session}] ${String(data).trim()}`))
    child.stderr?.on('data', (data: Buffer) => this.log.debug(`[sala ${session}] ${String(data).trim()}`))

    const room: Room = {
      session,
      mapId,
      mapName: requestedName ?? mapId,
      host: this.config.gameServerPublicHost,
      port,
      players: 0,
      playerNames: [],
      capacity: 32,
      startedAt: now(),
      lastActivity: now(),
      launchToken,
      tokenIssuedAt: now(),
      chat: [],
      child,
      settled: false,
    }

    await new Promise<void>((resolve, reject) => {
      const timeoutMs = binary ? 30_000 : 180_000
      const started = now()
      const timer = setTimeout(() => reject(new Error('a sala demorou demais para abrir')), timeoutMs)
      const fail = (message: string): void => {
        clearTimeout(timer)
        reject(new Error(message))
      }
      let lastError = ''
      const watch = (buffer: Buffer): void => {
        const text = buffer.toString()
        // Falha explícita do Go (porta ocupada, mapa inválido, flag desconhecida).
        if (/udp:|mapa:|tcp:|flag provided|Usage of/.test(text)) lastError = text.trim()
        // UDP não aceita TCP probe; o log "no ar em" é o sinal de pronto.
        if (text.includes('no ar em')) {
          clearTimeout(timer)
          room.settled = true
          resolve()
        }
      }
      child.stdout?.on('data', watch)
      child.stderr?.on('data', watch)
      child.once('exit', (code) => {
        this.rooms.delete(mapId)
        this.reservedPorts.delete(port)
        if (!room.settled) fail(`a sala terminou antes de abrir (código ${code}) ${lastError}`)
      })
      // Modo TCP (testes): a porta aceita conexão; UDP: não dá para sondar, o
      // log do próprio jogo é a confirmação.
      if (!this.config.tcpMode) return
      const probe = (): void => {
        if (now() - started > timeoutMs) return fail('a sala demorou demais para abrir')
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
    })

    this.reservedPorts.delete(port)
    this.rooms.set(mapId, room)
    this.log.info(`sala ${session} de "${mapId}" no ar em ${room.host}:${port}`)
    return this.publicRoom(room)
  }

  validateJoin(mapId: string, token: string): Room | null {
    const room = this.rooms.get(mapId)
    if (!room || token !== room.launchToken) return null
    if (now() - room.tokenIssuedAt > this.config.launchTtlMs) return null
    return room
  }

  report(input: { session: number; port: number; players?: string[]; chat?: { from: string; text: string } | null }): Room | null {
    const room = [...this.rooms.values()].find((entry) => entry.session === input.session && entry.port === input.port)
    if (!room) return null
    const names = Array.isArray(input.players) ? input.players.slice(0, 64) : []
    room.players = names.length
    room.playerNames = names
    room.lastActivity = now()
    if (input.chat?.text) {
      room.chat.push(input.chat)
      if (room.chat.length > 20) room.chat.shift()
    }
    return room
  }

  async close(session: number): Promise<void> {
    for (const [mapId, room] of this.rooms) {
      if (room.session !== session) continue
      this.rooms.delete(mapId)
      room.child.kill('SIGTERM')
    }
  }

  private reap(): void {
    const cutoff = now() - this.config.roomIdleMs
    for (const [mapId, room] of this.rooms) {
      if (room.players === 0 && room.lastActivity < cutoff) {
        this.log.info(`sala ${room.session} (${mapId}) ociosa: encerrando`)
        this.rooms.delete(mapId)
        room.child.kill('SIGTERM')
      }
    }
  }

  shutdown(): void {
    clearInterval(this.timer)
    for (const room of this.rooms.values()) room.child.kill('SIGTERM')
    this.rooms.clear()
  }
}

/** Link customizado que o navegador abre para acionar o cliente Loomia. */
export function launchUrl(
  room: { host: string; port: number; launchToken: string; mapId: string },
  options: { playerName?: string; color?: string; origin?: string } = {},
): string {
  const query = new URLSearchParams({
    server: `${room.host}:${room.port}`,
    token: room.launchToken,
    map: room.mapId,
    name: options.playerName ?? 'Loomian',
    color: options.color ?? 'eabe19',
  })
  if (options.origin) query.set('origin', options.origin)
  return `loomia://launch?${query.toString()}`
}
