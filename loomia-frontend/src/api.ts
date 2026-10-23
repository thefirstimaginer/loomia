// Tipos compartilhados com o master server (loomia-masterserver) e a sala Go.
// As mesmas estruturas de `loomia-gameserver/world.go` e dos handlers Node.

export interface MapInfo {
  id: string
  name: string
  description: string
  author: string
  max_players: number
  updated_at: string
  /** Jogadores na sala (quando ela está aberta). */
  online?: number
  room_open?: boolean
}

export interface Block {
  position: [number, number, number]
  size: [number, number, number]
  color: [number, number, number]
  material: 'plastic' | 'grass' | 'wood' | 'brick' | 'stone' | 'neon'
  collidable?: boolean
}

export interface WorldMap {
  id: string
  name: string
  author?: string
  spawn: [number, number, number]
  sky_color: [number, number, number]
  blocks: Block[]
  version?: string
}

export interface Appearance {
  username: string
  body_color: [number, number, number]
  shirt_color: [number, number, number]
  pants_color: [number, number, number]
  hat?: 'cap' | 'top_hat' | 'propeller' | 'crown' | null
}

export interface RoomInfo {
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

export interface Status {
  name: string
  version: string
  uptime_secs: number
  tick_rate: number
  players_online: number
  sessions: number
  maps: number
  rooms: {
    map_id: string
    map_name: string
    players: number
    capacity: number
    address: string
    started_at: string
  }[]
  protocol: { scheme: string; handler: string; registered: boolean }
}

/** O master aloca a sala e devolve o link `loomia://` que o navegador abre. */
export interface LaunchResponse {
  ok: boolean
  map_id: string
  server: string
  token: string
  launch_url: string
  message: string
}

/** O site fala com o master server. Em dev, o Vite faz proxy de /api. */
export async function api<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await fetch(path, {
    headers: { 'Content-Type': 'application/json' },
    ...init,
  })
  const text = await response.text()
  const data = text ? JSON.parse(text) : {}
  if (!response.ok) {
    throw new Error((data as { error?: string }).error ?? `HTTP ${response.status}`)
  }
  return data as T
}
