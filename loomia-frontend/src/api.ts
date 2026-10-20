// Tipos compartilhados com o servidor (as mesmas estruturas de `loomia-shared`).

export interface WorldInfo {
  id: string
  name: string
  description: string
  max_players: number
  player_count: number
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
  spawn: [number, number, number]
  sky_color: [number, number, number]
  blocks: Block[]
}

export interface Appearance {
  username: string
  body_color: [number, number, number]
  shirt_color: [number, number, number]
  pants_color: [number, number, number]
  hat?: 'cap' | 'top_hat' | 'propeller' | 'crown' | null
}

export interface ServerStatus {
  version: string
  uptime_secs: number
  tick_rate: number
  ws_url: string
  players_online: number
  sessions: number
  total_connections: number
  total_chat_messages: number
  map: { name: string; blocks: number }
  players: { id: number; name: string; position: [number, number, number] }[]
  client: { built: boolean; path: string }
  frontend: { dir: string; ready: boolean }
}

export interface LaunchResponse {
  ok?: boolean
  pid?: number
  message?: string
  built_now?: boolean
  error?: string
}

export interface LaunchStatus {
  built: boolean
  building: boolean
  last_line: string
  binary: string
}

/** O site fala com a API do servidor Loomia. Em dev, o Vite faz proxy de /api. */
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
