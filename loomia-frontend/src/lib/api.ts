// Cliente da API do Loomia.
//
// Todas as chamadas usam cookies de sessão (`credentials: 'include'`), então o
// site funciona tanto no Vite (proxy) quanto servido pelo master.

export interface Avatar {
  id: string
  name: string
  bodyColor: [number, number, number]
  shirtColor: [number, number, number]
  pantsColor: [number, number, number]
  hat: 'cap' | 'top_hat' | 'propeller' | 'crown' | null
  isActive: boolean
  createdAt: string
}

export interface User {
  id: string
  firstName: string
  lastName: string
  nickname: string
  email: string
  createdAt: string
  lastLoginAt: string | null
}

export interface SessionInfo {
  user: User | null
  hasAvatar: boolean
  avatars: Avatar[]
}

export interface MapInfo {
  id: string
  name: string
  description: string
  author: string
  authorId: string | null
  maxPlayers: number
  blocks: number
  version: string
  visibility: 'public' | 'private'
  createdAt: string
  updatedAt: string
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
  description?: string
  author?: string
  spawn: [number, number, number]
  sky_color: [number, number, number]
  blocks: Block[]
  version?: string
}

export interface LaunchResponse {
  ok: boolean
  map_id: string
  server: string
  token: string
  launch_url: string
  arguments: string[]
  avatar: Avatar | null
  message: string
}

export interface AvatarOptions {
  hats: { id: string; label: string }[]
  bodyColors: string[]
  shirtColors: string[]
  pantsColors: string[]
}

export interface AvatarPayload {
  name: string
  bodyColor: [number, number, number]
  shirtColor: [number, number, number]
  pantsColor: [number, number, number]
  hat: Avatar['hat']
  isActive?: boolean
}

export class ApiError extends Error {
  status: number
  code?: string

  constructor(status: number, message: string, code?: string) {
    super(message)
    this.name = 'ApiError'
    this.status = status
    this.code = code
  }
}

async function request<T>(path: string, init: RequestInit = {}): Promise<T> {
  const response = await fetch(path, {
    credentials: 'include',
    headers: { 'Content-Type': 'application/json', ...(init.headers ?? {}) },
    ...init,
  })
  const text = await response.text()
  const data = text ? JSON.parse(text) : {}
  if (!response.ok) {
    throw new ApiError(response.status, (data as { error?: string }).error ?? `HTTP ${response.status}`, (data as { code?: string }).code)
  }
  return data as T
}

export const api = {
  session: () => request<SessionInfo>('/api/auth/me'),

  register: (input: {
    firstName: string
    lastName: string
    nickname: string
    email: string
    password: string
    passwordConfirm: string
  }) => request<{ user: User; next: string }>('/api/auth/register', { method: 'POST', body: JSON.stringify(input) }),

  login: (input: { login: string; password: string }) =>
    request<{ user: User; hasAvatar: boolean; avatars: Avatar[] }>('/api/auth/login', {
      method: 'POST',
      body: JSON.stringify(input),
    }),

  logout: () => request<{ ok: true }>('/api/auth/logout', { method: 'POST' }),

  updateProfile: (input: { firstName?: string; lastName?: string }) =>
    request<{ user: User }>('/api/me', { method: 'PUT', body: JSON.stringify(input) }),

  avatars: () => request<{ avatars: Avatar[]; limit: number }>('/api/me/avatars'),

  createAvatar: (input: AvatarPayload) =>
    request<{ avatar: Avatar; count: number }>('/api/me/avatars', { method: 'POST', body: JSON.stringify(input) }),

  updateAvatar: (id: string, input: AvatarPayload) =>
    request<{ avatar: Avatar }>(`/api/me/avatars/${id}`, { method: 'PUT', body: JSON.stringify(input) }),

  activateAvatar: (id: string) =>
    request<{ avatar: Avatar }>(`/api/me/avatars/${id}/activate`, { method: 'POST' }),

  deleteAvatar: (id: string) =>
    request<{ ok: true; avatars: Avatar[] }>(`/api/me/avatars/${id}`, { method: 'DELETE' }),

  avatarOptions: () => request<AvatarOptions>('/api/avatar-options'),

  maps: (search = '') => request<MapInfo[]>(`/api/maps${search ? `?q=${encodeURIComponent(search)}` : ''}`),

  map: (id: string) => request<WorldMap>(`/api/maps/${id}`),

  myMaps: () => request<{ maps: MapInfo[] }>('/api/corner/maps'),

  publishMap: (input: { name: string; description: string; visible: boolean; world: WorldMap }) =>
    request<{ map: MapInfo }>('/api/corner/maps', { method: 'POST', body: JSON.stringify(input) }),

  updateMap: (id: string, input: { name: string; description: string; visible: boolean; world: WorldMap }) =>
    request<{ map: MapInfo }>(`/api/corner/maps/${id}`, { method: 'PUT', body: JSON.stringify(input) }),

  launch: (mapId: string) =>
    request<LaunchResponse>(`/api/maps/${mapId}/launch`, { method: 'POST', body: JSON.stringify({ mapId }) }),

  clientInfo: () =>
    request<{ name: string; platform: string; format: string; registered: boolean; protocol: string }>(
      '/api/client/info',
    ),

  status: () =>
    request<{ players_online: number; rooms: { map_id: string; map_name: string; players: number }[] }>('/api/status'),
}

export function rgbToHex(color: [number, number, number]): string {
  return '#' + color.map((entry) => entry.toString(16).padStart(2, '0')).join('')
}

export function hexToRgb(hex: string): [number, number, number] {
  const value = hex.replace('#', '')
  const full = value.length === 3 ? value.split('').map((char) => char + char).join('') : value
  const parsed = parseInt(full, 16)
  return [(parsed >> 16) & 255, (parsed >> 8) & 255, parsed & 255]
}
