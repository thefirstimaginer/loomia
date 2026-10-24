// Rotas do painel de administração (loomia-admin).
//
// Todas exigem papel `admin` (leitura e escrita), exceto os relatórios internos
// das salas, que usam o segredo compartilhado.

import type { RequestContext, Router } from './http.js'
import { HttpError, sendJson } from './http.js'
import { config } from './config.js'
import { requireAdmin, ONLINE_WINDOW_MINUTES } from './auth/session.js'
import {
  banPlayer,
  dashboardStats,
  findPlayerById,
  listReports,
  listTickets,
  removePlayer,
  searchPlayers,
  unbanPlayer,
  updateReport,
  updateTicket,
  type ReportStatus,
  type TicketStatus,
} from './db/moderation.js'
import { query } from './db/index.js'

const ADMIN_ROLES: ReportStatus[] = ['open', 'reviewing', 'resolved', 'dismissed']
const TICKET_ROLES: TicketStatus[] = ['open', 'reviewing', 'resolved', 'closed']

function text(value: unknown, field: string, max: number): string {
  const result = typeof value === 'string' ? value.trim() : ''
  if (result.length > max) throw new HttpError(400, `${field} é longo demais`, 'invalid')
  return result
}

function playerId(value: string | undefined): string {
  const id = Number(value)
  if (!Number.isInteger(id) || id <= 0) throw new HttpError(400, 'id de jogador inválido', 'invalid')
  return String(id)
}

function requireOrigin(ctx: RequestContext, allowed: string[]): void {
  const origin = ctx.req.headers.origin
  if (!origin) return // chamadas diretas (curl, testes) não enviam Origin
  if (!allowed.includes(origin)) {
    throw new HttpError(403, `origem ${origin} não autorizada para o painel`, 'origin_forbidden')
  }
}

export function createAdminApi(router: Router): void {
  const guard = (ctx: RequestContext) => {
    requireOrigin(ctx, config.adminOrigins)
    return requireAdmin(ctx.req, ctx.cookies, 'admin')
  }

  // Credenciais exigidas pelo painel (nunca expõe a lista de admins).
  router.get(/^\/api\/admin\/me$/, async (ctx) => {
    const admin = await guard(ctx)
    sendJson(ctx.res, 200, { admin })
  })

  // ------------------------------------------------------------------ dashboard

  router.get(/^\/api\/admin\/dashboard$/, async (ctx) => {
    await guard(ctx)
    const stats = await dashboardStats(ONLINE_WINDOW_MINUTES)
    const recent = await query<Record<string, unknown>>(
      `SELECT id, kind, created_at FROM account_events ORDER BY created_at DESC LIMIT 12`,
    )
    sendJson(ctx.res, 200, {
      ...stats,
      online_window_minutes: ONLINE_WINDOW_MINUTES,
      recent_events: recent.rows.map((row) => ({
        id: String(row.id),
        kind: String(row.kind),
        createdAt: new Date(row.created_at as Date).toISOString(),
      })),
    })
  })

  // ------------------------------------------------------------------ jogadores

  router.get(/^\/api\/admin\/players$/, async (ctx) => {
    await guard(ctx)
    const term = ctx.query.get('q') ?? ''
    const status = ctx.query.get('status') ?? ''
    const players = await searchPlayers(term, 100, ONLINE_WINDOW_MINUTES)
    sendJson(
      ctx.res,
      200,
      { players: status ? players.filter((player) => player.status === status) : players },
    )
  })

  router.get(/^\/api\/admin\/players\/(\d+)$/, async (ctx) => {
    await guard(ctx)
    const player = await findPlayerById(playerId(ctx.params[0]), ONLINE_WINDOW_MINUTES)
    if (!player) throw new HttpError(404, 'jogador não encontrado', 'not_found')
    const [reportsAgainst, tickets] = await Promise.all([
      listReports(undefined, 100),
      listTickets({ authorId: player.id, limit: 20 }),
    ])
    sendJson(ctx.res, 200, {
      player,
      reports: reportsAgainst.filter((report) => report.targetId === player.id),
      tickets,
    })
  })

  /** Exclui a conta: email e apelido voltam a ser livres. */
  router.delete(/^\/api\/admin\/players\/(\d+)$/, async (ctx) => {
    const admin = await guard(ctx)
    const id = playerId(ctx.params[0])
    if (id === admin.id) throw new HttpError(400, 'não dá para excluir a própria conta por aqui', 'invalid')
    const removed = await removePlayer(id, admin.id)
    if (!removed) throw new HttpError(404, 'jogador não encontrado', 'not_found')
    sendJson(ctx.res, 200, { ok: true, removed: true })
  })

  router.post(/^\/api\/admin\/players\/(\d+)\/ban$/, async (ctx) => {
    const admin = await guard(ctx)
    const body = (await ctx.body()) as { reason?: unknown }
    const reason = text(body.reason, 'motivo', 500) || 'violação das regras'
    const banned = await banPlayer(playerId(ctx.params[0]), { id: admin.id, nickname: admin.nickname }, reason)
    if (!banned) throw new HttpError(404, 'jogador não encontrado', 'not_found')
    sendJson(ctx.res, 200, { ok: true, banned: true })
  })

  router.post(/^\/api\/admin\/players\/(\d+)\/unban$/, async (ctx) => {
    const admin = await guard(ctx)
    const unbanned = await unbanPlayer(playerId(ctx.params[0]), admin.id)
    if (!unbanned) throw new HttpError(404, 'jogador não encontrado', 'not_found')
    sendJson(ctx.res, 200, { ok: true, banned: false })
  })

  // ------------------------------------------------------------------ reports

  router.get(/^\/api\/admin\/reports$/, async (ctx) => {
    await guard(ctx)
    sendJson(ctx.res, 200, { reports: await listReports(ctx.query.get('status') ?? '') })
  })

  router.put(/^\/api\/admin\/reports\/(\d+)$/, async (ctx) => {
    const admin = await guard(ctx)
    const body = (await ctx.body()) as Record<string, unknown>
    const status = String(body.status ?? '') as ReportStatus
    if (!ADMIN_ROLES.includes(status)) throw new HttpError(400, 'status inválido', 'invalid')
    const report = await updateReport(String(Number(ctx.params[0])), status, text(body.resolution, 'resolução', 1000), admin.id)
    if (!report) throw new HttpError(404, 'report não encontrado', 'not_found')
    sendJson(ctx.res, 200, { report })
  })

  // ------------------------------------------------------------------ tickets

  router.get(/^\/api\/admin\/tickets$/, async (ctx) => {
    await guard(ctx)
    sendJson(ctx.res, 200, {
      tickets: await listTickets({ status: ctx.query.get('status') ?? '' }),
    })
  })

  router.put(/^\/api\/admin\/tickets\/(\d+)$/, async (ctx) => {
    const admin = await guard(ctx)
    const body = (await ctx.body()) as Record<string, unknown>
    const status = String(body.status ?? '') as TicketStatus
    if (!TICKET_ROLES.includes(status)) throw new HttpError(400, 'status inválido', 'invalid')
    const ticket = await updateTicket(String(Number(ctx.params[0])), status, text(body.resolution, 'resolução', 1000), admin.id)
    if (!ticket) throw new HttpError(404, 'ticket não encontrado', 'not_found')
    sendJson(ctx.res, 200, { ticket })
  })
}
