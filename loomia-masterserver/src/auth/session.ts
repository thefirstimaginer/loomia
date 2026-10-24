// Sessões de login.
//
// O cookie guarda um token aleatório; o banco guarda apenas o SHA-256 dele.
// Sessões antigas expiram sozinhas (30 dias) e podem ser revogadas (logout).
//
// A sessão é revalidada a cada requisição: conta banida ou removida perde o
// acesso na hora, mesmo que o token ainda esteja válido.

import type { IncomingMessage } from 'node:http'
import { hashToken, newSessionToken } from '../auth/password.js'
import { one, query } from '../db/index.js'
import { HttpError } from '../http.js'

export const SESSION_COOKIE = 'loomia_session'
const TTL_MS = 30 * 24 * 60 * 60 * 1000
/** Consideramos "online" quem teve atividade nos últimos 5 minutos. */
export const ONLINE_WINDOW_MINUTES = 5

export type Role = 'player' | 'moderator' | 'admin'
export type AccountStatus = 'active' | 'offline' | 'removed' | 'banned'

export interface SessionUser {
  id: string
  firstName: string
  lastName: string
  nickname: string | null
  email: string
  bio: string
  role: Role
  status: AccountStatus
  createdAt: string
  lastLoginAt: string | null
  lastSeenAt: string | null
}

export async function createSession(userId: string, userAgent: string): Promise<{ token: string; expiresAt: Date }> {
  const token = newSessionToken()
  const expiresAt = new Date(Date.now() + TTL_MS)
  await query(
    `INSERT INTO sessions (token_hash, user_id, user_agent, expires_at) VALUES ($1, $2, $3, $4)`,
    [hashToken(token), userId, userAgent.slice(0, 255), expiresAt],
  )
  return { token, expiresAt }
}

export async function deleteSession(token: string): Promise<void> {
  await query('UPDATE sessions SET revoked_at = now() WHERE token_hash = $1 AND revoked_at IS NULL', [hashToken(token)])
}

/** Encerra todas as sessões de uma conta (usado no ban). */
export async function revokeAllSessions(userId: string): Promise<void> {
  await query('UPDATE sessions SET revoked_at = now() WHERE user_id = $1 AND revoked_at IS NULL', [userId])
}

function toSessionUser(row: Record<string, unknown>): SessionUser {
  return {
    id: String(row.id),
    firstName: String(row.first_name),
    lastName: String(row.last_name),
    nickname: (row.nickname as string | null) ?? null,
    email: String(row.email),
    bio: String(row.bio ?? ''),
    role: row.role as Role,
    status: row.status as AccountStatus,
    createdAt: new Date(row.created_at as Date).toISOString(),
    lastLoginAt: row.last_login_at ? new Date(row.last_login_at as Date).toISOString() : null,
    lastSeenAt: row.last_seen_at ? new Date(row.last_seen_at as Date).toISOString() : null,
  }
}

/**
 * Usuário dono do token (ou null se inválido/expirado/banido/removido).
 * Marca o último acesso (é o que alimenta o gráfico de "online").
 */
export async function userForToken(token: string | undefined): Promise<SessionUser | null> {
  if (!token) return null
  const row = await one<Record<string, unknown>>(
    `WITH touched AS (
       UPDATE sessions SET user_agent = user_agent
        WHERE token_hash = $1 AND revoked_at IS NULL AND expires_at > now()
        RETURNING user_id
     )
     UPDATE users u
        SET last_seen_at = now()
      WHERE u.id = (SELECT user_id FROM touched)
        AND u.status NOT IN ('banned', 'removed')
      RETURNING u.*`,
    [hashToken(token)],
  )
  return row ? toSessionUser(row) : null
}

/** Igual a `userForToken`, mas lança 401 quando não há sessão. */
export async function requireUser(req: IncomingMessage, cookies: Map<string, string>): Promise<SessionUser> {
  void req
  const user = await userForToken(cookies.get(SESSION_COOKIE))
  if (!user) throw new HttpError(401, 'entre na sua conta para continuar', 'unauthenticated')
  return user
}

/** Exige papel de admin (ou moderador, para leitura e moderação leve). */
export async function requireAdmin(
  req: IncomingMessage,
  cookies: Map<string, string>,
  level: 'moderator' | 'admin' = 'admin',
): Promise<SessionUser> {
  const user = await requireUser(req, cookies)
  const allowed = level === 'admin' ? user.role === 'admin' : user.role === 'admin' || user.role === 'moderator'
  if (!allowed) throw new HttpError(403, 'acesso restrito à equipe do Loomia', 'forbidden')
  return user
}

export { toSessionUser }
