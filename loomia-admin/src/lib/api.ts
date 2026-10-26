// Tipos e cliente da API do painel de administração.

export type Role = 'player' | 'moderator' | 'admin'

export interface AdminUser {
  id: string
  firstName: string
  lastName: string
  nickname: string | null
  email: string
  bio: string
  role: Role
  status: 'active' | 'offline' | 'removed' | 'banned'
  createdAt: string
  lastLoginAt: string | null
  lastSeenAt: string | null
}

export interface AdminPlayer extends AdminUser {
  maps: number
  online: boolean
  bannedAt: string | null
}

export interface AdminReport {
  id: string
  reporterId: string | null
  reporterName: string
  targetId: string | null
  targetName: string
  description: string
  status: 'open' | 'reviewing' | 'resolved' | 'dismissed'
  mapId: string | null
  resolution: string
  createdAt: string
}

export interface AdminTicket {
  id: string
  authorId: string | null
  authorName: string
  category: string
  subject: string
  body: string
  status: 'open' | 'reviewing' | 'resolved' | 'closed'
  resolution: string
  createdAt: string
}

export interface AdminCaptcha {
  token: string
  svg: string
}
export interface Dashboard {
  users: { online: number; offline: number; total: number }
  registrations: {
    last24h: number
    last7d: number
    total: number
    series: { date: string; count: number }[]
  }
  moderation: { banned: number; removed: number; active: number; series: { kind: string; count: number }[] }
  maps: {
    published: number
    public: number
    private: number
    byAuthor: number
    series: { date: string; count: number }[]
  }
  reports: { open: number; reviewing: number; resolved: number }
  tickets: { open: number; reviewing: number; resolved: number }
  online_window_minutes: number
  recent_events: { id: string; kind: string; createdAt: string }[]
}

// O painel é servido em /admin, mas a API vive na raiz do master (/api).
// Em dev o proxy do Vite cuida disso; em produção usamos o caminho absoluto.
const BASE = import.meta.env.DEV ? '' : ''

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
  const response = await fetch(`${BASE}${path}`, {
    credentials: 'include',
    headers: { 'Content-Type': 'application/json', ...(init.headers ?? {}) },
    ...init,
  })
  const text = await response.text()
  const data = text ? JSON.parse(text) : {}
  if (!response.ok) {
    throw new ApiError(
      response.status,
      (data as { error?: string }).error ?? `HTTP ${response.status}`,
      (data as { code?: string }).code,
    )
  }
  return data as T
}

export const adminApi = {
  me: () => request<{ admin: AdminUser }>('/api/admin/me'),
  login: (input: { login: string; password: string }) =>
    request<{ user: AdminUser; hasAvatar: boolean }>('/api/auth/login', {
      method: 'POST',
      body: JSON.stringify(input),
    }),
  logout: () => request<{ ok: true }>('/api/auth/logout', { method: 'POST' }),

  dashboard: () => request<Dashboard>('/api/admin/dashboard'),

  players: (term = '', status = '') =>
    request<{ players: AdminPlayer[] }>(
      `/api/admin/players?q=${encodeURIComponent(term)}${status ? `&status=${status}` : ''}`,
    ),

  player: (id: string) =>
    request<{ player: AdminPlayer; reports: AdminReport[]; tickets: AdminTicket[] }>(`/api/admin/players/${id}`),

  removePlayer: (id: string) => request<{ ok: true }>(`/api/admin/players/${id}`, { method: 'DELETE' }),

  banPlayer: (id: string, reason: string) =>
    request<{ ok: true }>(`/api/admin/players/${id}/ban`, {
      method: 'POST',
      body: JSON.stringify({ reason }),
    }),

  unbanPlayer: (id: string) =>
    request<{ ok: true }>(`/api/admin/players/${id}/unban`, { method: 'POST' }),

  reports: (status = '') => request<{ reports: AdminReport[] }>(`/api/admin/reports?status=${status}`),

  updateReport: (id: string, status: string, resolution: string) =>
    request<{ report: AdminReport }>(`/api/admin/reports/${id}`, {
      method: 'PUT',
      body: JSON.stringify({ status, resolution }),
    }),

  tickets: (status = '') => request<{ tickets: AdminTicket[] }>(`/api/admin/tickets?status=${status}`),

  updateTicket: (id: string, status: string, resolution: string) =>
    request<{ ticket: AdminTicket }>(`/api/admin/tickets/${id}`, {
      method: 'PUT',
      body: JSON.stringify({ status, resolution }),
    }),
  createAdmin: (input: {
    firstName: string
    lastName: string
    nickname: string
    email: string
    password: string
    passwordConfirm: string
    captchaToken: string
    captchaAnswer: string
  }) => request<{ user: AdminUser }>('/api/admin/register', {
    method: 'POST',
    body: JSON.stringify(input),
  }),

  captcha: (): Promise<AdminCaptcha> => request<AdminCaptcha>('/api/admin/captcha'),
}

export function formatDate(iso: string | null): string {
  if (!iso) return '—'
  return new Date(iso).toLocaleString('pt-BR', { dateStyle: 'short', timeStyle: 'short' })
}

export function relative(iso: string | null): string {
  if (!iso) return 'nunca'
  const diff = Date.now() - new Date(iso).getTime()
  const minutes = Math.round(diff / 60_000)
  if (minutes < 1) return 'agora'
  if (minutes < 60) return `há ${minutes} min`
  const hours = Math.round(minutes / 60)
  if (hours < 24) return `há ${hours} h`
  return `há ${Math.round(hours / 24)} d`
}
