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
import { findUserByLogin, createUser, toPublicUser } from './db/repo.js'
import { hashPassword } from './auth/password.js'
import { parseRegister } from './validate.js'
import { recordEvent } from './db/moderation.js'
import { RateLimiter } from './util.js'

// Captchas pendentes: token -> resposta (expira em 10 minutos).
const CAPTCHA_TTL_MS = 10 * 60_000
const captchaStore = new Map<string, { answer: string; expiresAt: number }>()

function captchaAlphabet(): string {
  // Sem I/L/O/0/1 para não confundir quem lê o código distorcido.
  return 'ABCDEFGHJKMNPQRSTUVWXYZ23456789'
}


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
  const limiter = new RateLimiter()
  const guard = (ctx: RequestContext) => {
    requireOrigin(ctx, config.adminOrigins)
    return requireAdmin(ctx.req, ctx.cookies, 'admin')
  }

  // Credenciais exigidas pelo painel (nunca expõe a lista de admins).
  router.get(/^\/api\/admin\/me$/, async (ctx) => {
    const admin = await guard(ctx)
    sendJson(ctx.res, 200, { admin })
  })

  // ------------------------------------------------------------------ criar admin

  /**
   * Emite um novo desafio de captcha: token + SVG distorcido com o código.
   * A resposta nunca vai para o cliente — só a imagem.
   */
  router.get(/^\/api\/admin\/captcha$/, async (ctx) => {
    requireOrigin(ctx, config.adminOrigins)
    const answer = Array.from({ length: 5 }, () => captchaAlphabet()[Math.floor(Math.random() * captchaAlphabet().length)]).join('')
    const token = `captcha-${Date.now()}-${Math.random().toString(36).slice(2)}`
    captchaStore.set(token, { answer, expiresAt: Date.now() + CAPTCHA_TTL_MS })
    for (const [key, entry] of captchaStore) if (entry.expiresAt < Date.now()) captchaStore.delete(key)

    const colors = ['#16202c', '#c8931a', '#2e8b57', '#0f78be']
    const glyphs = answer
      .split('')
      .map((char, index) => {
        const x = 18 + index * 28 + (Math.random() * 8 - 4)
        const y = 32 + (Math.random() * 8 - 4)
        const rotate = (Math.random() - 0.5) * 40
        const color = colors[index % colors.length]
        return `<text x="${x.toFixed(1)}" y="${y.toFixed(1)}" transform="rotate(${rotate.toFixed(1)} ${x.toFixed(1)} ${y.toFixed(1)})" fill="${color}" font-family="Comic Neue, sans-serif" font-size="30" font-weight="700" text-anchor="middle">${char}</text>`
      })
      .join('')
    const noise = Array.from({ length: 5 }, () => {
      const x1 = Math.random() * 150
      const y1 = Math.random() * 44
      const x2 = Math.random() * 150
      const y2 = Math.random() * 44
      return `<line x1="${x1.toFixed(1)}" y1="${y1.toFixed(1)}" x2="${x2.toFixed(1)}" y2="${y2.toFixed(1)}" stroke="rgba(200,147,26,0.4)" stroke-width="1.5"/>`
    }).join('')
    const dots = Array.from({ length: 30 }, () => {
      const x = Math.random() * 150
      const y = Math.random() * 44
      return `<circle cx="${x.toFixed(1)}" cy="${y.toFixed(1)}" r="1.2" fill="rgba(22,32,44,0.18)"/>`
    }).join('')
    const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="150" height="44" viewBox="0 0 150 44"><rect width="150" height="44" fill="#eef2f7"/>${dots}${noise}${glyphs}</svg>`

    sendJson(ctx.res, 200, { token, svg })
  })

  /**
   * Cria uma conta de administrador. Exige um admin já logado (guard acima),
   * captcha válido e email/apelido livres — o backend nunca confia no cliente.
   */
  router.post(/^\/api\/admin\/register$/, async (ctx) => {
    const admin = await guard(ctx)
    const wait = limiter.take(`admin-register:${ctx.ip}`, 5, 60 * 60_000)
    if (wait > 0) throw new HttpError(429, `muitas tentativas, tente em ${wait}s`, 'rate_limited')

    const body = (await ctx.body()) as Record<string, unknown>
    const challenge = captchaStore.get(String(body.captchaToken ?? ''))
    const answer = String(body.captchaAnswer ?? '').trim().toUpperCase()
    if (!challenge || challenge.expiresAt < Date.now() || challenge.answer !== answer) {
      captchaStore.delete(String(body.captchaToken ?? ''))
      throw new HttpError(400, 'captcha inválido ou expirado', 'captcha_invalid')
    }
    captchaStore.delete(String(body.captchaToken ?? ''))

    const input = parseRegister(body)
    if (await findUserByLogin(input.nickname)) {
      throw new HttpError(409, 'esse apelido já está em uso', 'nickname_taken')
    }
    const passwordHash = await hashPassword(input.password)
    const created = await createUser({ ...input, passwordHash })
    await query("UPDATE users SET role = 'admin' WHERE id = $1", [String(created.id)])
    await recordEvent('registered', String(created.id))
    sendJson(ctx.res, 201, { user: toPublicUser({ ...created, role: 'admin' }) })
    void admin
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
