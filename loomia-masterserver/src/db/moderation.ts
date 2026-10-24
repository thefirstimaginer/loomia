// Consultas de moderação, dashboard e recursos sociais.
//
// Tudo que o painel admin e o site do jogador precisam além de contas e mapas.

import { one, query, transaction } from '../db/index.js'
import { HttpError } from '../http.js'

// ---------------------------------------------------------------------------
// Dashboard
// ---------------------------------------------------------------------------

export interface DashboardStats {
  users: { online: number; offline: number; total: number }
  registrations: { last24h: number; last7d: number; total: number; series: { date: string; count: number }[] }
  moderation: { banned: number; removed: number; active: number; series: { kind: string; count: number }[] }
  maps: { published: number; public: number; private: number; byAuthor: number; series: { date: string; count: number }[] }
  reports: { open: number; reviewing: number; resolved: number }
  tickets: { open: number; reviewing: number; resolved: number }
}

export async function dashboardStats(onlineWindowMinutes: number): Promise<DashboardStats> {
  const [users, regs, mod, maps, reports, tickets] = await Promise.all([
    one<{ online: string; offline: string; total: string }>(
      `SELECT
         count(*) FILTER (WHERE status <> 'banned' AND last_seen_at > now() - ($1 || ' minutes')::interval) AS online,
         count(*) FILTER (WHERE status <> 'banned' AND (last_seen_at IS NULL OR last_seen_at <= now() - ($1 || ' minutes')::interval)) AS offline,
         count(*) AS total
       FROM users`,
      [String(onlineWindowMinutes)],
    ),
    one<{ day1: string; day7: string; total: string }>(
      `SELECT
         count(*) FILTER (WHERE created_at > now() - interval '1 day') AS day1,
         count(*) FILTER (WHERE created_at > now() - interval '7 days') AS day7,
         count(*) AS total
       FROM users`,
    ),
    one<{ banned: string; removed: string; active: string }>(
      `SELECT
         (SELECT count(*) FROM bans) AS banned,
         count(*) FILTER (WHERE status = 'removed') AS removed,
         count(*) FILTER (WHERE status <> 'removed') AS active
       FROM users`,
    ),
    one<{ published: string; public: string; private: string; by_author: string }>(
      `SELECT count(*) AS published,
              count(*) FILTER (WHERE visible) AS public,
              count(*) FILTER (WHERE NOT visible) AS private,
              count(DISTINCT author_id) AS by_author
         FROM maps`,
    ),
    one<{ open: string; reviewing: string; resolved: string }>(
      `SELECT count(*) FILTER (WHERE status = 'open') AS open,
              count(*) FILTER (WHERE status = 'reviewing') AS reviewing,
              count(*) FILTER (WHERE status IN ('resolved', 'dismissed')) AS resolved
         FROM reports`,
    ),
    one<{ open: string; reviewing: string; resolved: string }>(
      `SELECT count(*) FILTER (WHERE status = 'open') AS open,
              count(*) FILTER (WHERE status = 'reviewing') AS reviewing,
              count(*) FILTER (WHERE status IN ('resolved', 'closed')) AS resolved
         FROM tickets`,
    ),
  ])

  const registrationsSeries = await query<{ date: string; count: string }>(
    `SELECT to_char(d.day, 'YYYY-MM-DD') AS date, count(u.id)::text AS count
       FROM generate_series(current_date - interval '13 days', current_date, interval '1 day') AS d(day)
       LEFT JOIN users u ON u.created_at::date = d.day
      GROUP BY d.day ORDER BY d.day`,
  )

  const mapsSeries = await query<{ date: string; count: string }>(
    `SELECT to_char(d.day, 'YYYY-MM-DD') AS date, count(m.id)::text AS count
       FROM generate_series(current_date - interval '13 days', current_date, interval '1 day') AS d(day)
       LEFT JOIN maps m ON m.created_at::date = d.day
      GROUP BY d.day ORDER BY d.day`,
  )

  const moderationSeries = await query<{ kind: string; count: string }>(
    `SELECT kind, count(*)::text AS count FROM account_events
      WHERE kind IN ('banned', 'removed') AND created_at > now() - interval '30 days'
      GROUP BY kind`,
  )

  return {
    users: {
      online: Number(users?.online ?? 0),
      offline: Number(users?.offline ?? 0),
      total: Number(users?.total ?? 0),
    },
    registrations: {
      last24h: Number(regs?.day1 ?? 0),
      last7d: Number(regs?.day7 ?? 0),
      total: Number(regs?.total ?? 0),
      series: registrationsSeries.rows.map((row) => ({ date: row.date, count: Number(row.count) })),
    },
    moderation: {
      banned: Number(mod?.banned ?? 0),
      removed: Number(mod?.removed ?? 0),
      active: Number(mod?.active ?? 0),
      series: moderationSeries.rows.map((row) => ({ kind: row.kind, count: Number(row.count) })),
    },
    maps: {
      published: Number(maps?.published ?? 0),
      public: Number(maps?.public ?? 0),
      private: Number(maps?.private ?? 0),
      byAuthor: Number(maps?.by_author ?? 0),
      series: mapsSeries.rows.map((row) => ({ date: row.date, count: Number(row.count) })),
    },
    reports: {
      open: Number(reports?.open ?? 0),
      reviewing: Number(reports?.reviewing ?? 0),
      resolved: Number(reports?.resolved ?? 0),
    },
    tickets: {
      open: Number(tickets?.open ?? 0),
      reviewing: Number(tickets?.reviewing ?? 0),
      resolved: Number(tickets?.resolved ?? 0),
    },
  }
}

// ---------------------------------------------------------------------------
// Jogadores (admin)
// ---------------------------------------------------------------------------

export interface AdminPlayer {
  id: string
  firstName: string
  lastName: string
  nickname: string | null
  email: string
  bio: string
  role: string
  status: string
  createdAt: string
  lastSeenAt: string | null
  maps: number
  online: boolean
  bannedAt: string | null
}

function toAdminPlayer(row: Record<string, unknown>, onlineWindowMinutes: number): AdminPlayer {
  const lastSeen = row.last_seen_at ? new Date(row.last_seen_at as Date) : null
  return {
    id: String(row.id),
    firstName: String(row.first_name),
    lastName: String(row.last_name),
    nickname: (row.nickname as string | null) ?? null,
    email: String(row.email),
    bio: String(row.bio ?? ''),
    role: String(row.role),
    status: String(row.status),
    createdAt: new Date(row.created_at as Date).toISOString(),
    lastSeenAt: lastSeen ? lastSeen.toISOString() : null,
    maps: Number(row.map_count ?? 0),
    online: Boolean(row.status === 'active') && !!lastSeen && Date.now() - lastSeen.getTime() < onlineWindowMinutes * 60_000,
    bannedAt: row.banned_at ? new Date(row.banned_at as Date).toISOString() : null,
  }
}

const PLAYER_SELECT = `
  SELECT u.*, (SELECT count(*) FROM maps m WHERE m.author_id = u.id)::int AS map_count
    FROM users u`

export async function searchPlayers(term: string, limit = 50, onlineWindowMinutes = 5): Promise<AdminPlayer[]> {
  const like = `%${term.trim()}%`
  const rows = await query(
    `${PLAYER_SELECT}
      WHERE $1 = '%%'
         OR u.nickname ILIKE $1 OR u.email ILIKE $1
         OR u.first_name ILIKE $1 OR u.last_name ILIKE $1
      ORDER BY u.created_at DESC LIMIT $2`,
    [like || '%%', limit],
  )
  return rows.rows.map((row) => toAdminPlayer(row, onlineWindowMinutes))
}

export async function findPlayerById(id: string, onlineWindowMinutes = 5): Promise<AdminPlayer | null> {
  const row = await one(`${PLAYER_SELECT} WHERE u.id = $1`, [id])
  return row ? toAdminPlayer(row, onlineWindowMinutes) : null
}

export async function isEmailBanned(email: string): Promise<{ banned: boolean; reason?: string }> {
  const row = await one<{ reason: string }>(
    `SELECT reason FROM bans
      WHERE lower(email_lower) = lower($1) AND (expires_at IS NULL OR expires_at > now())
      ORDER BY created_at DESC LIMIT 1`,
    [email],
  )
  return row ? { banned: true, reason: row.reason } : { banned: false }
}

export async function recordEvent(
  kind: 'registered' | 'login' | 'removed' | 'banned' | 'unbanned' | 'restored',
  userId: string | null,
  actorId: string | null = null,
): Promise<void> {
  await query('INSERT INTO account_events (user_id, kind, actor_id) VALUES ($1, $2, $3)', [userId, kind, actorId])
}

/**
 * Exclui a conta: os dados pessoais vão embora, mas o email/apelido ficam
 * livres para um novo cadastro (regra do produto).
 */
export async function removePlayer(userId: string, actorId: string): Promise<boolean> {
  return transaction(async (client) => {
    const user = await client.query<{ email: string; nickname: string | null }>(
      'SELECT email, nickname FROM users WHERE id = $1',
      [userId],
    )
    if (user.rowCount === 0) return false
    await client.query('DELETE FROM users WHERE id = $1', [userId])
    await client.query('INSERT INTO account_events (user_id, kind, actor_id) VALUES (NULL, $1, $2)', ['removed', actorId])
    return true
  })
}

/**
 * Bane: mantém a conta bloqueada, derruba as sessões, queima o email e libera o
 * apelido (SET NULL) para outra pessoa usar.
 */
export async function banPlayer(
  userId: string,
  actor: { id: string; nickname: string | null },
  reason: string,
): Promise<boolean> {
  return transaction(async (client) => {
    const user = await client.query<{ email: string; nickname: string | null; role: string }>(
      'SELECT email, nickname, role FROM users WHERE id = $1 FOR UPDATE',
      [userId],
    )
    if (user.rowCount === 0) return false
    const target = user.rows[0]!
    if (target.role === 'admin') throw new HttpError(403, 'não é possível banir um administrador', 'forbidden')

    await client.query(
      `UPDATE users SET status = 'banned', banned_at = now(), nickname = NULL, updated_at = now()
        WHERE id = $1`,
      [userId],
    )
    await client.query(
      `INSERT INTO bans (user_id, email_lower, nickname, reason, banned_by, banned_by_name)
       VALUES ($1, lower($2), $3, $4, $5, $6)`,
      [userId, target.email, target.nickname, reason, actor.id, actor.nickname ?? 'admin'],
    )
    await client.query('UPDATE sessions SET revoked_at = now() WHERE user_id = $1::bigint AND revoked_at IS NULL', [userId])
    await client.query('INSERT INTO account_events (user_id, kind, actor_id) VALUES ($1, $2, $3)', [userId, 'banned', actor.id])
    return true
  })
}

/** Desfaz o ban (o email volta a ser utilizável). */
export async function unbanPlayer(userId: string, actorId: string): Promise<boolean> {
  return transaction(async (client) => {
    const user = await client.query<{ email: string; status: string }>(
      'SELECT email, status FROM users WHERE id = $1',
      [userId],
    )
    if (user.rowCount === 0) return false
    await client.query(
      `UPDATE users SET status = 'offline', banned_at = NULL, updated_at = now() WHERE id = $1`,
      [userId],
    )
    await client.query('DELETE FROM bans WHERE user_id = $1::bigint', [userId])
    await client.query('INSERT INTO account_events (user_id, kind, actor_id) VALUES ($1, $2, $3)', [userId, 'unbanned', actorId])
    return true
  })
}

// ---------------------------------------------------------------------------
// Reports
// ---------------------------------------------------------------------------

export type ReportStatus = 'open' | 'reviewing' | 'resolved' | 'dismissed'

export interface Report {
  id: string
  reporterId: string | null
  reporterName: string
  targetId: string | null
  targetName: string
  description: string
  status: ReportStatus
  mapId: string | null
  resolution: string
  createdAt: string
}

function toReport(row: Record<string, unknown>): Report {
  return {
    id: String(row.id),
    reporterId: row.reporter_id ? String(row.reporter_id) : null,
    reporterName: String(row.reporter_name),
    targetId: row.target_id ? String(row.target_id) : null,
    targetName: String(row.target_name),
    description: String(row.description),
    status: row.status as ReportStatus,
    mapId: (row.map_id as string | null) ?? null,
    resolution: String(row.resolution ?? ''),
    createdAt: new Date(row.created_at as Date).toISOString(),
  }
}

export async function createReport(input: {
  reporterId: string
  reporterName: string
  targetId: string
  targetName: string
  description: string
  mapId?: string | null
}): Promise<Report> {
  const row = await one(
    `INSERT INTO reports (reporter_id, reporter_name, target_id, target_name, description, map_id)
     VALUES ($1, $2, $3, $4, $5, $6) RETURNING *`,
    [input.reporterId, input.reporterName, input.targetId, input.targetName, input.description, input.mapId ?? null],
  )
  return toReport(row!)
}

export async function listReports(status?: string, limit = 100): Promise<Report[]> {
  const rows = await query(
    `SELECT * FROM reports WHERE ($1 = '' OR status = $1) ORDER BY created_at DESC LIMIT $2`,
    [status ?? '', limit],
  )
  return rows.rows.map(toReport)
}

export async function updateReport(
  id: string,
  status: ReportStatus,
  resolution: string,
  adminId: string,
): Promise<Report | null> {
  const row = await one(
    `UPDATE reports SET status = $2, resolution = $3, resolved_by = $4::bigint, updated_at = now()
      WHERE id = $1 RETURNING *`,
    [id, status, resolution, adminId],
  )
  return row ? toReport(row) : null
}

// ---------------------------------------------------------------------------
// Tickets
// ---------------------------------------------------------------------------

export type TicketStatus = 'open' | 'reviewing' | 'resolved' | 'closed'

export interface Ticket {
  id: string
  authorId: string | null
  authorName: string
  category: string
  subject: string
  body: string
  status: TicketStatus
  resolution: string
  createdAt: string
}

function toTicket(row: Record<string, unknown>): Ticket {
  return {
    id: String(row.id),
    authorId: row.author_id ? String(row.author_id) : null,
    authorName: String(row.author_name),
    category: String(row.category),
    subject: String(row.subject),
    body: String(row.body),
    status: row.status as TicketStatus,
    resolution: String(row.resolution ?? ''),
    createdAt: new Date(row.created_at as Date).toISOString(),
  }
}

export async function createTicket(input: {
  authorId: string
  authorName: string
  category: string
  subject: string
  body: string
}): Promise<Ticket> {
  const row = await one(
    `INSERT INTO tickets (author_id, author_name, category, subject, body)
     VALUES ($1, $2, $3, $4, $5) RETURNING *`,
    [input.authorId, input.authorName, input.category, input.subject, input.body],
  )
  return toTicket(row!)
}

export async function listTickets(options: { status?: string; authorId?: string; limit?: number } = {}): Promise<Ticket[]> {
  const rows = await query(
    `SELECT * FROM tickets
      WHERE ($1 = '' OR status = $1)
        AND ($2 = '' OR author_id = NULLIF($2, '')::bigint)
      ORDER BY created_at DESC LIMIT $3`,
    [options.status ?? '', options.authorId ?? '', options.limit ?? 100],
  )
  return rows.rows.map(toTicket)
}

export async function updateTicket(
  id: string,
  status: TicketStatus,
  resolution: string,
  adminId: string,
): Promise<Ticket | null> {
  const row = await one(
    `UPDATE tickets SET status = $2, resolution = $3, resolved_by = $4::bigint, updated_at = now()
      WHERE id = $1 RETURNING *`,
    [id, status, resolution, adminId],
  )
  return row ? toTicket(row) : null
}
