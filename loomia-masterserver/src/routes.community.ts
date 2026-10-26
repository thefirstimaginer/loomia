// Rotas de comunidade do site: detalhe do mundo (visitas/estrelas), fórum,
// blog e a rede de jogadores (aba Amigos).
//
//   Login:  /api/maps/:id/detail  /api/maps/:id/star  /api/forum/*  /api/players?all=1
//   Público: /api/blog/posts  /api/blog/posts/:slug

import type { RequestContext, Router } from './http.js'
import { HttpError, sendJson } from './http.js'
import type { SessionUser } from './auth/session.js'
import { findMap } from './db/repo.js'
import {
  createForumPost,
  createForumRoom,
  findForumRoom,
  listBlogPosts,
  findBlogPost,
  listForumPosts,
  listForumRooms,
  mapDetail,
  toggleStar,
} from './db/community.js'
import { listNetworkPlayers } from './db/community.js'

function text(value: unknown, field: string, min: number, max: number): string {
  const result = typeof value === 'string' ? value.trim() : ''
  if (result.length < min) throw new HttpError(400, `${field} é obrigatório`, 'invalid')
  if (result.length > max) throw new HttpError(400, `${field} é longo demais`, 'invalid')
  return result
}

export function createCommunityApi(router: Router, guard: (ctx: RequestContext) => Promise<SessionUser>): void {
  // ------------------------------------------------------------------ mundo

  router.get(/^\/api\/maps\/([\w-]+)\/detail$/, async (ctx) => {
    const user = await guard(ctx)
    const detail = await mapDetail(ctx.params[0]!, user.id)
    if (!detail) throw new HttpError(404, 'mapa não encontrado', 'not_found')
    sendJson(ctx.res, 200, detail)
  })

  router.post(/^\/api\/maps\/([\w-]+)\/star$/, async (ctx) => {
    const user = await guard(ctx)
    const mapId = ctx.params[0]!
    if (!(await findMap(mapId))) throw new HttpError(404, 'mapa não encontrado', 'not_found')
    sendJson(ctx.res, 200, await toggleStar(mapId, user.id))
  })

  // ------------------------------------------------------------------ fórum

  router.get(/^\/api\/forum\/rooms$/, async (ctx) => {
    await guard(ctx)
    sendJson(ctx.res, 200, { rooms: await listForumRooms() })
  })

  router.post(/^\/api\/forum\/rooms$/, async (ctx) => {
    const user = await guard(ctx)
    const body = (await ctx.body()) as Record<string, unknown>
    const room = await createForumRoom({
      authorId: user.id,
      authorName: user.nickname ?? user.firstName,
      title: text(body.title, 'título', 3, 120),
      description: text(body.description ?? '', 'descrição', 0, 500),
    })
    sendJson(ctx.res, 201, { room })
  })

  router.get(/^\/api\/forum\/rooms\/(\d+)\/posts$/, async (ctx) => {
    await guard(ctx)
    const room = await findForumRoom(Number(ctx.params[0]))
    if (!room) throw new HttpError(404, 'sala não encontrada', 'not_found')
    sendJson(ctx.res, 200, { room, posts: await listForumPosts(Number(ctx.params[0])) })
  })

  router.post(/^\/api\/forum\/rooms\/(\d+)\/posts$/, async (ctx) => {
    const user = await guard(ctx)
    const roomId = Number(ctx.params[0])
    if (!(await findForumRoom(roomId))) throw new HttpError(404, 'sala não encontrada', 'not_found')
    const body = (await ctx.body()) as Record<string, unknown>
    const post = await createForumPost({
      roomId,
      authorId: user.id,
      authorName: user.nickname ?? user.firstName,
      body: text(body.body, 'mensagem', 1, 2000),
    })
    sendJson(ctx.res, 201, { post })
  })

  // ------------------------------------------------------------------ blog (público)

  router.get(/^\/api\/blog\/posts$/, async (ctx) => {
    sendJson(ctx.res, 200, { posts: await listBlogPosts() })
  })

  router.get(/^\/api\/blog\/posts\/([\w-]+)$/, async (ctx) => {
    const post = await findBlogPost(ctx.params[0]!)
    if (!post) throw new HttpError(404, 'notícia não encontrada', 'not_found')
    sendJson(ctx.res, 200, post)
  })

  // ------------------------------------------------------------------ rede

  /** Todos os jogadores da rede (com avatar) — aba Amigos. */
  router.get(/^\/api\/network\/players$/, async (ctx) => {
    await guard(ctx)
    sendJson(ctx.res, 200, { players: await listNetworkPlayers() })
  })
}
