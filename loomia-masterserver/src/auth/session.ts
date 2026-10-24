// Sessões de login.
//
// O cookie guarda um token aleatório; o banco guarda apenas o SHA-256 dele.
// Sessões antigas expiram sozinhas (30 dias) e podem ser revogadas (logout).

import type { IncomingMessage } from 'node:http'
import { hashToken, newSessionToken } from '../auth/password.js'
import { one, query } from '../db/index.js'
import { HttpError } from '../http.js'

export const SESSION_COOKIE = 'loomia_session'
const TTL_MS = 30 * 24 * 60 * 60 * 1000

export interface SessionUser {
  id: string
  firstName: string
  lastName: string
  nickname: string
  email: string
  createdAt: string
  lastLoginAt: string | null
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

/** Usuário dono do token (ou null se inválido/expirado). */
export async function userForToken(token: string | undefined): Promise<SessionUser | null> {
  if (!token) return null
  const row = await one<{
    id: string
    first_name: string
    last_name: string
    nickname: string
    email: string
    created_at: Date
    last_login_at: Date | null
  }>(
    `SELECT u.id, u.first_name, u.last_name, u.nickname, u.email, u.created_at, u.last_login_at
       FROM sessions s
       JOIN users u ON u.id = s.user_id
      WHERE s.token_hash = $1 AND s.revoked_at IS NULL AND s.expires_at > now()`,
    [hashToken(token)],
  )
  if (!row) return null
  return {
    id: String(row.id),
    firstName: row.first_name,
    lastName: row.last_name,
    nickname: row.nickname,
    email: row.email,
    createdAt: row.created_at.toISOString(),
    lastLoginAt: row.last_login_at ? row.last_login_at.toISOString() : null,
  }
}

/** Igual a `userForToken`, mas lança 401 quando não há sessão. */
export async function requireUser(req: IncomingMessage, cookies: Map<string, string>): Promise<SessionUser> {
  const user = await userForToken(cookies.get(SESSION_COOKIE))
  if (!user) throw new HttpError(401, 'entre na sua conta para continuar', 'unauthenticated')
  return user
}
