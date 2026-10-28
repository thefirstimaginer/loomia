// Rotas do jogador: perfil público, reportar, favoritos, amizades, mensagens,
// tickets de suporte e bio.

import type { RequestContext, Router } from './http.js'
import { HttpError, sendJson } from './http.js'
import type { SessionUser } from './auth/session.js'
import { findMap, findUserById } from './db/repo.js'
import {
  createTicket,
  listTickets,
} from './db/moderation.js'
import { listFavoriteSafe } from './db/socialSearch.js'
import {
  listFriendRequests,
  listFriends,
  listMessages,
  publicProfile,
  respondFriendRequest,
  sendFriendRequest,
  sendMessage,
  setFavorite,
  unreadCount,
  updateBio,
} from './db/social.js'
import { createReport } from './db/moderation.js'
import { parseId, parseText } from './validate.js'

const TICKET_CATEGORIES = ['client', 'account', 'gameplay', 'suggestion', 'other'] as const

const text = parseText

/** Id do jogador (perfil, amizade, conversa) vindo da URL ou do corpo. */
const targetId = (value: string | undefined): string => parseId(value, 'id de jogador')

export function createSocialApi(router: Router, guard: (ctx: RequestContext) => Promise<SessionUser>): void {
  // ------------------------------------------------------------------ perfil público

  router.get(/^\/api\/players\/(\d+)$/, async (ctx) => {
    const viewer = await guard(ctx)
    const profile = await publicProfile(viewer.id, targetId(ctx.params[0]))
    if (!profile) throw new HttpError(404, 'jogador não encontrado', 'not_found')
    sendJson(ctx.res, 200, profile)
  })

  /** Busca jogadores pelo apelido (para achar alguém e abrir o perfil). */
  router.get(/^\/api\/players$/, async (ctx) => {
    await guard(ctx)
    const term = (ctx.query.get('q') ?? '').trim()
    const players = await listFavoriteSafe(term, 30)
    sendJson(ctx.res, 200, { players })
  })

  router.put(/^\/api\/me\/bio$/, async (ctx) => {
    const user = await guard(ctx)
    const body = (await ctx.body()) as Record<string, unknown>
    await updateBio(user.id, text(body.bio ?? '', 'bio', 0, 500))
    sendJson(ctx.res, 200, { ok: true })
  })

  // ------------------------------------------------------------------ reportar

  router.post(/^\/api\/reports$/, async (ctx) => {
    const reporter = await guard(ctx)
    const body = (await ctx.body()) as Record<string, unknown>
    const target = await findUserById(targetId(String(body.targetId ?? '')))
    if (!target) throw new HttpError(404, 'jogador não encontrado', 'not_found')
    if (String(target.id) === reporter.id) throw new HttpError(400, 'não dá para reportar você mesmo', 'invalid')

    const description = text(body.description, 'descrição', 10, 2000)
    const mapId = typeof body.mapId === 'string' && body.mapId ? body.mapId : null
    if (mapId && !(await findMap(mapId))) throw new HttpError(404, 'mapa não encontrado', 'not_found')

    const report = await createReport({
      reporterId: reporter.id,
      reporterName: reporter.nickname ?? reporter.firstName,
      targetId: String(target.id),
      targetName: target.nickname ?? `${target.first_name} ${target.last_name}`,
      description,
      mapId,
    })
    sendJson(ctx.res, 201, { report })
  })

  // ------------------------------------------------------------------ favoritos

  router.post(/^\/api\/players\/(\d+)\/favorite$/, async (ctx) => {
    const user = await guard(ctx)
    await setFavorite(user.id, targetId(ctx.params[0]), true)
    sendJson(ctx.res, 200, { favorite: true })
  })

  router.delete(/^\/api\/players\/(\d+)\/favorite$/, async (ctx) => {
    const user = await guard(ctx)
    await setFavorite(user.id, targetId(ctx.params[0]), false)
    sendJson(ctx.res, 200, { favorite: false })
  })

  router.get(/^\/api\/me\/favorites$/, async (ctx) => {
    const user = await guard(ctx)
    sendJson(ctx.res, 200, { favorites: await listFavoriteSafe('', 100, user.id) })
  })

  // ------------------------------------------------------------------ amizades

  router.get(/^\/api\/me\/friends$/, async (ctx) => {
    const user = await guard(ctx)
    sendJson(ctx.res, 200, { friends: await listFriends(user.id), requests: await listFriendRequests(user.id) })
  })

  router.post(/^\/api\/friends\/requests$/, async (ctx) => {
    const user = await guard(ctx)
    const body = (await ctx.body()) as Record<string, unknown>
    const friendId = targetId(String(body.targetId ?? ''))
    if (friendId === user.id) throw new HttpError(400, 'não dá para adicionar você mesmo', 'invalid')
    const message = typeof body.message === 'string' ? body.message.trim().slice(0, 500) : ''
    const request = await sendFriendRequest(user.id, friendId, message)
    sendJson(ctx.res, 201, { request })
  })

  router.post(/^\/api\/friends\/requests\/(\d+)\/accept$/, async (ctx) => {
    const user = await guard(ctx)
    const request = await respondFriendRequest(String(Number(ctx.params[0])), user.id, 'accepted')
    sendJson(ctx.res, 200, { request })
  })

  router.post(/^\/api\/friends\/requests\/(\d+)\/decline$/, async (ctx) => {
    const user = await guard(ctx)
    const request = await respondFriendRequest(String(Number(ctx.params[0])), user.id, 'declined')
    sendJson(ctx.res, 200, { request })
  })

  router.delete(/^\/api\/friends\/requests\/(\d+)$/, async (ctx) => {
    const user = await guard(ctx)
    const request = await respondFriendRequest(String(Number(ctx.params[0])), user.id, 'cancelled')
    sendJson(ctx.res, 200, { request })
  })

  // ------------------------------------------------------------------ mensagens

  router.get(/^\/api\/messages\/(\d+)$/, async (ctx) => {
    const user = await guard(ctx)
    const other = targetId(ctx.params[0])
    sendJson(ctx.res, 200, { messages: await listMessages(user.id, other), unread: await unreadCount(user.id) })
  })

  router.post(/^\/api\/messages\/(\d+)$/, async (ctx) => {
    const user = await guard(ctx)
    const body = (await ctx.body()) as Record<string, unknown>
    const message = await sendMessage(user.id, targetId(ctx.params[0]), text(body.body, 'mensagem', 1, 1000))
    sendJson(ctx.res, 201, { message })
  })

  router.get(/^\/api\/me\/unread$/, async (ctx) => {
    const user = await guard(ctx)
    sendJson(ctx.res, 200, { unread: await unreadCount(user.id) })
  })

  // ------------------------------------------------------------------ tickets

  router.get(/^\/api\/me\/tickets$/, async (ctx) => {
    const user = await guard(ctx)
    sendJson(ctx.res, 200, { tickets: await listTickets({ authorId: user.id, limit: 50 }) })
  })

  router.post(/^\/api\/tickets$/, async (ctx) => {
    const user = await guard(ctx)
    const body = (await ctx.body()) as Record<string, unknown>
    const category = String(body.category ?? 'other')
    if (!(TICKET_CATEGORIES as readonly string[]).includes(category)) {
      throw new HttpError(400, 'categoria inválida', 'invalid')
    }
    const ticket = await createTicket({
      authorId: user.id,
      authorName: user.nickname ?? user.firstName,
      category,
      subject: text(body.subject, 'assunto', 3, 120),
      body: text(body.body, 'descrição', 10, 4000),
    })
    sendJson(ctx.res, 201, { ticket })
  })
}
