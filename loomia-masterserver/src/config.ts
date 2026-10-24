// Configuração do master server (TypeScript + PostgreSQL).

import path from 'node:path'
import { fileURLToPath } from 'node:url'
import { existsSync } from 'node:fs'

const here = path.dirname(fileURLToPath(import.meta.url))

/**
 * Raiz do repositório (`loomia/`).
 *
 * `import.meta.url` aponta para `loomia-masterserver/src/db/` (fonte) ou
 * `loomia-masterserver/dist/src/db/` (build) — então subimos até encontrar a
 * pasta `loomia-masterserver`, o que funciona nos dois casos e também com
 * `tsx`.
 */
function findRepoRoot(start: string): string {
  let current = start
  for (let depth = 0; depth < 8; depth++) {
    if (existsSync(path.join(current, 'loomia-masterserver'))) return current
    const parent = path.dirname(current)
    if (parent === current) break
    current = parent
  }
  return path.resolve(start, '..', '..', '..', '..')
}

const REPO_ROOT = findRepoRoot(here)
/** `loomia-masterserver/`. */
export const ROOT = path.join(REPO_ROOT, 'loomia-masterserver')
const PROJECT = REPO_ROOT

/** Resolve um caminho relativo à raiz do repositório. */
const resolveFromProject = (value: string): string =>
  path.isAbsolute(value) ? value : path.resolve(PROJECT, value)

/** Resolve um caminho relativo a `loomia-masterserver/`. */
const resolveFromServer = (value: string): string =>
  path.isAbsolute(value) ? value : path.resolve(ROOT, value)

const num = (value: string | undefined, fallback: number): number => {
  const parsed = Number(value)
  return Number.isFinite(parsed) ? parsed : fallback
}

const bool = (value: string | undefined, fallback = false): boolean => {
  if (value === undefined) return fallback
  const normalized = value.toLowerCase()
  return normalized === '1' || normalized === 'true' || normalized === 'yes'
}

export interface Config {
  host: string
  bind: string
  port: number

  databaseUrl: string
  databaseSsl: boolean

  frontendDir: string
  worldsDir: string
  uploadsDir: string

  roomSecret: string
  gameServerBin: string
  gameServerGoDir: string
  gameServerGo: string
  gameServerBind: string
  gameServerPublicHost: string
  gameServerPortMin: number
  gameServerPortMax: number
  maxRooms: number
  roomIdleMs: number
  launchTtlMs: number
  tcpMode: boolean
  clientBin: string
  serverName: string
  sessionSecret: string
}

export const config: Config = {
  host: process.env.LOOMIA_HOST ?? '127.0.0.1',
  bind: process.env.LOOMIA_BIND ?? '127.0.0.1',
  port: num(process.env.LOOMIA_PORT ?? process.env.PORT, 8080),

  databaseUrl:
    process.env.DATABASE_URL ??
    process.env.LOOMIA_DATABASE_URL ??
    'postgresql://loomia:loomia@127.0.0.1:5432/loomia',
  databaseSsl: bool(process.env.LOOMIA_DATABASE_SSL),

  frontendDir: resolveFromProject(process.env.LOOMIA_FRONTEND_DIR ?? 'loomia-frontend/dist'),
  worldsDir: resolveFromProject(process.env.LOOMIA_WORLDS_DIR ?? 'data/worlds'),
  uploadsDir: resolveFromProject(process.env.LOOMIA_UPLOADS_DIR ?? 'data/uploads'),

  roomSecret: process.env.LOOMIA_ROOM_SECRET ?? 'loomia-dev-secret',
  gameServerBin: resolveFromServer(process.env.LOOMIA_GAMESERVER_BIN ?? '../loomia-gameserver/loomia-gameserver'),
  gameServerGoDir: resolveFromProject(process.env.LOOMIA_GAMESERVER_DIR ?? 'loomia-gameserver'),
  gameServerGo: process.env.LOOMIA_GO ?? 'go',
  gameServerBind: process.env.LOOMIA_GAMESERVER_BIND ?? '127.0.0.1',
  gameServerPublicHost: process.env.LOOMIA_GAMESERVER_PUBLIC_HOST ?? '127.0.0.1',
  gameServerPortMin: num(process.env.LOOMIA_GAMESERVER_PORT_MIN, 7800),
  gameServerPortMax: num(process.env.LOOMIA_GAMESERVER_PORT_MAX, 7899),
  maxRooms: num(process.env.LOOMIA_MAX_ROOMS, 8),
  roomIdleMs: num(process.env.LOOMIA_ROOM_IDLE_MS, 2 * 60 * 1000),
  launchTtlMs: num(process.env.LOOMIA_LAUNCH_TTL_MS, 5 * 60 * 1000),
  tcpMode: bool(process.env.LOOMIA_TCP),
  clientBin: process.env.LOOMIA_CLIENT_BIN ?? 'loomia-client',
  serverName: process.env.LOOMIA_SERVER_NAME ?? 'Loomia',
  sessionSecret: process.env.LOOMIA_SESSION_SECRET ?? '',
}
