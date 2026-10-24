// Rotas da API do Loomia.
//
//   Público:  /api/health /api/auth/* /api/maps /api/maps/:id /api/avatar-options
//   Login:    /api/me /api/me/avatars /api/maps/:id/launch /api/corner/*
//   Sala:     /api/internal/rooms/* (segredo compartilhado)
//
// Regra do produto: entrar em um mapa e acessar a Loomier's Corner exige login.

import type { ServerResponse } from 'node:http'
import { readFile, writeFile, mkdir, access, readdir } from 'node:fs/promises'
import path from 'node:path'
import { createHash, randomUUID } from 'node:crypto'

import type { Config } from './config.js'
import {
  activateAvatar,
  createAvatar,
  createUser,
  countAvatars,
  deleteAvatar,
  findMap,
  findUserByLogin,
  findUserById,
  listAvatars,
  listMapsByAuthor,
  listPublicMaps,
  toAvatar,
  toMapInfo,
  toPublicUser,
  touchLogin,
  updateAvatar,
  updateProfile,
} from './db/repo.js'
import { query } from './db/index.js'
import { DUMMY_HASH, hashPassword, verifyPassword } from './auth/password.js'
import { SESSION_COOKIE, createSession, deleteSession, requireUser, userForToken, type SessionUser } from './auth/session.js'
import { HttpError, Router, clientIp, readBody, sendJson, serializeCookie, type RequestContext } from './http.js'
import { inspectProtocolHandler, launchArguments } from './protocol.js'
import { isEmailBanned, recordEvent } from './db/moderation.js'
import { createAdminApi } from './routes.admin.js'
import { createSocialApi } from './routes.social.js'
import { config as appConfig } from './config.js'
import { RoomManager, launchUrl } from './rooms.js'
import { RateLimiter, type Logger } from './util.js'
import { parseAvatar, parseLogin, parseMapId, parseMapPublish, parseRegister } from './validate.js'

const HATS = [
  { id: 'cap', label: 'Boné' },
  { id: 'top_hat', label: 'Cartola' },
  { id: 'propeller', label: 'Hélice' },
  { id: 'crown', label: 'Coroa' },
] as const

const BODY_COLORS = ['#eabe19', '#e0af8c', '#8c5a3c', '#f5d2aa', '#78c88c', '#bebec8']
const SHIRT_COLORS = ['#0f78be', '#be2828', '#e6e6e6', '#464646', '#f0a014', '#6e3ca0']
const PANTS_COLORS = ['#234b2d', '#1e1e28', '#5a4632', '#3c3c78', '#787878']

const MAX_AVATARS = 12

export function hexToRgb(hex: string): [number, number, number] {
  const value = hex.replace('#', '')
  const full = value.length === 3 ? value.split('').map((char) => char + char).join('') : value
  const parsed = parseInt(full, 16)
  return [(parsed >> 16) & 255, (parsed >> 8) & 255, parsed & 255]
}

function rgbToHex(color: number[]): string {
  return '#' + color.map((entry) => entry.toString(16).padStart(2, '0')).join('')
}

function mapVersion(world: { blocks: unknown }): string {
  const digest = createHash('sha256').update(JSON.stringify(world.blocks)).digest('hex').slice(0, 12)
  return digest
}

export interface ApiDeps {
  config: Config
  rooms: RoomManager
  log: Logger
  startedAt: number
}

export function createApi(deps: ApiDeps): Router {
  const { config, rooms, log, startedAt } = deps
  const limiter = new RateLimiter()
  const router = new Router()

  const guard = async (ctx: RequestContext): Promise<SessionUser> => requireUser(ctx.req, ctx.cookies)

  const setSessionCookie = (ctx: RequestContext, token: string, maxAgeSeconds: number): void => {
    ctx.res.setHeader(
      'set-cookie',
      serializeCookie(SESSION_COOKIE, token, {
        maxAge: maxAgeSeconds,
        httpOnly: true,
        secure: (ctx.req.headers['x-forwarded-proto'] ?? '') === 'https',
        sameSite: 'Lax',
      }),
    )
  }

  const clearSessionCookie = (ctx: RequestContext): void => {
    ctx.res.setHeader('set-cookie', serializeCookie(SESSION_COOKIE, '', { maxAge: 0, httpOnly: true, sameSite: 'Lax' }))
  }

  // ------------------------------------------------------------------ saúde

  router.get(/^\/api\/health$/, (ctx) => {
    sendJson(ctx.res, 200, { ok: true, uptime_secs: Math.floor((Date.now() - startedAt) / 1000) })
  })

  router.get(/^\/api\/avatar-options$/, (ctx) => {
    sendJson(ctx.res, 200, { hats: HATS, bodyColors: BODY_COLORS, shirtColors: SHIRT_COLORS, pantsColors: PANTS_COLORS })
  })

  // ------------------------------------------------------------------ contas

  router.post(/^\/api\/auth\/register$/, async (ctx) => {
    const wait = limiter.take(`register:${ctx.ip}`, config.limitRegister, 60_000)
    if (wait > 0) throw new HttpError(429, `muitas tentativas, tente em ${wait}s`, 'rate_limited')

    const input = parseRegister(await ctx.body())

    // Email banido não pode criar conta nova — nem contar como apelido usado.
    const banned = await isEmailBanned(input.email)
    if (banned.banned) {
      throw new HttpError(403, 'esse email está bloqueado no Loomia', 'email_banned')
    }

    // Apelido: checagem explícita para dar erro claro mesmo quando o email
    // pertence a uma conta banida (o apelido dela já foi liberado).
    if (await findUserByLogin(input.nickname)) {
      throw new HttpError(409, 'esse apelido já está em uso', 'nickname_taken')
    }

    const passwordHash = await hashPassword(input.password)
    const user = await createUser({ ...input, passwordHash })

    // Promoção a admin por lista de emails confiável (LOOMIA_ADMIN_EMAILS).
    if (appConfig.adminEmails.includes(input.email.toLowerCase())) {
      await query("UPDATE users SET role = 'admin' WHERE id = $1", [String(user.id)])
      user.role = 'admin'
    }
    await recordEvent('registered', String(user.id))

    const session = await createSession(String(user.id), String(ctx.req.headers['user-agent'] ?? ''))
    setSessionCookie(ctx, session.token, 30 * 24 * 3600)

    sendJson(ctx.res, 201, {
      user: toPublicUser(user),
      /** O site manda o jogador para o editor do primeiro avatar. */
      next: 'onboarding',
      role: user.role,
      session: { expiresAt: session.expiresAt.toISOString() },
    })
  })

  router.post(/^\/api\/auth\/login$/, async (ctx) => {
    const wait = limiter.take(`login:${ctx.ip}`, config.limitLogin, 60_000)
    if (wait > 0) throw new HttpError(429, `muitas tentativas, tente em ${wait}s`, 'rate_limited')

    const input = parseLogin(await ctx.body())
    const user = await findUserByLogin(input.login)
    // Compara mesmo sem usuário (tempo constante em relação a "existe conta").
    const ok = await verifyPassword(input.password, user?.password_hash ?? DUMMY_HASH)
    if (!user || !ok) throw new HttpError(401, 'email/apelido ou senha inválidos', 'invalid_credentials')

    if (user.status === 'banned' || user.status === 'removed') {
      throw new HttpError(403, 'essa conta está bloqueada', 'account_blocked')
    }
    await touchLogin(String(user.id))
    await query("UPDATE users SET status = 'active' WHERE id = $1 AND status <> 'banned'", [String(user.id)])
    await recordEvent('login', String(user.id))
    const session = await createSession(String(user.id), String(ctx.req.headers['user-agent'] ?? ''))
    setSessionCookie(ctx, session.token, 30 * 24 * 3600)

    const avatars = await listAvatars(String(user.id))
    sendJson(ctx.res, 200, {
      user: toPublicUser(user),
      hasAvatar: avatars.length > 0,
      avatars,
    })
  })

  router.post(/^\/api\/auth\/logout$/, async (ctx) => {
    const token = ctx.cookies.get(SESSION_COOKIE)
    if (token) await deleteSession(token)
    clearSessionCookie(ctx)
    sendJson(ctx.res, 200, { ok: true })
  })

  router.get(/^\/api\/auth\/me$/, async (ctx) => {
    const user = await userForToken(ctx.cookies.get(SESSION_COOKIE))
    if (!user) return sendJson(ctx.res, 200, { user: null, hasAvatar: false, avatars: [] })
    const avatars = await listAvatars(user.id)
    sendJson(ctx.res, 200, { user, hasAvatar: avatars.length > 0, avatars })
  })

  // alias curto usado por páginas antigas
  router.get(/^\/api\/me$/, async (ctx) => {
    const user = await guard(ctx)
    const avatars = await listAvatars(user.id)
    sendJson(ctx.res, 200, { user, avatars, hasAvatar: avatars.length > 0 })
  })

  router.put(/^\/api\/me$/, async (ctx) => {
    const user = await guard(ctx)
    const body = (await ctx.body()) as Record<string, unknown>
    const updated = await updateProfile(user.id, {
      firstName: typeof body.firstName === 'string' ? body.firstName.trim().slice(0, 60) : undefined,
      lastName: typeof body.lastName === 'string' ? body.lastName.trim().slice(0, 60) : undefined,
    })
    sendJson(ctx.res, 200, { user: updated ? toPublicUser(updated) : user })
  })

  // ------------------------------------------------------------------ avatares

  router.get(/^\/api\/me\/avatars$/, async (ctx) => {
    const user = await guard(ctx)
    sendJson(ctx.res, 200, { avatars: await listAvatars(user.id), limit: MAX_AVATARS })
  })

  router.post(/^\/api\/me\/avatars$/, async (ctx) => {
    const user = await guard(ctx)
    const input = parseAvatar(await ctx.body())
    const avatar = await createAvatar(user.id, input)
    sendJson(ctx.res, 201, { avatar, count: await countAvatars(user.id) })
  })

  router.put(/^\/api\/me\/avatars\/(\d+)$/, async (ctx) => {
    const user = await guard(ctx)
    const avatar = await updateAvatar(user.id, ctx.params[0]!, parseAvatar(await ctx.body()))
    if (!avatar) throw new HttpError(404, 'avatar não encontrado', 'not_found')
    sendJson(ctx.res, 200, { avatar })
  })

  router.post(/^\/api\/me\/avatars\/(\d+)\/activate$/, async (ctx) => {
    const user = await guard(ctx)
    const avatar = await activateAvatar(user.id, ctx.params[0]!)
    if (!avatar) throw new HttpError(404, 'avatar não encontrado', 'not_found')
    sendJson(ctx.res, 200, { avatar })
  })

  router.delete(/^\/api\/me\/avatars\/(\d+)$/, async (ctx) => {
    const user = await guard(ctx)
    const removed = await deleteAvatar(user.id, ctx.params[0]!)
    if (!removed) throw new HttpError(404, 'avatar não encontrado', 'not_found')
    sendJson(ctx.res, 200, { ok: true, avatars: await listAvatars(user.id) })
  })

  // ------------------------------------------------------------------ estado geral

  router.get(/^\/api\/status$/, async (ctx) => {
    const roomList = rooms.list()
    const players = roomList.reduce((total, room) => total + room.players, 0)
    sendJson(ctx.res, 200, {
      name: config.serverName,
      version: '2.0.0',
      uptime_secs: Math.floor((Date.now() - startedAt) / 1000),
      tick_rate: 30,
      players_online: players,
      rooms: roomList.map((room) => ({
        map_id: room.map_id,
        map_name: room.map_name,
        players: room.players,
        capacity: room.capacity,
        address: `${room.host}:${room.port}`,
        started_at: room.started_at,
      })),
      protocol: { scheme: 'loomia://', ...inspectProtocolHandler() },
    })
  })

  // ------------------------------------------------------------------ mapas públicos

  router.get(/^\/api\/maps$/, async (ctx) => {
    const search = ctx.query.get('q') ?? ''
    const maps = await listPublicMaps(search)
    const open = new Map(rooms.list().map((room) => [room.map_id, room]))
    sendJson(
      ctx.res,
      200,
      maps.map((map) => {
        const room = open.get(map.id)
        return { ...map, online: room?.players ?? 0, room_open: Boolean(room) }
      }),
    )
  })

  router.get(/^\/api\/maps\/([\w-]+)$/, async (ctx) => {
    const id = parseMapId(ctx.params[0])
    const meta = await findMap(id)
    if (!meta || !meta.visible) throw new HttpError(404, 'mapa não encontrado', 'not_found')
    const world = (await readWorld(config, id)) as Record<string, unknown>
    sendJson(ctx.res, 200, { ...world, version: meta.version })
  })

  // ------------------------------------------------------------------ Loomier's Corner

  router.get(/^\/api\/corner\/maps$/, async (ctx) => {
    const user = await guard(ctx)
    sendJson(ctx.res, 200, { maps: await listMapsByAuthor(user.id) })
  })

  router.post(/^\/api\/corner\/maps$/, async (ctx) => {
    const user = await guard(ctx)
    const wait = limiter.take(`publish:${user.id}`, 10, 60 * 60_000)
    if (wait > 0) throw new HttpError(429, 'muitas publicações, tente mais tarde', 'rate_limited')

    const payload = parseMapPublish(await ctx.body(8 * 1024 * 1024))
    const existing = await findMap(payload.id)
    if (existing && existing.author_id !== user.id) {
      throw new HttpError(409, 'esse id de mapa já pertence a outro criador', 'map_taken')
    }

    const version = mapVersion(payload.world as { blocks: unknown })
    await mkdir(config.worldsDir, { recursive: true })
    await writeFile(path.join(config.worldsDir, `${payload.id}.loom`), JSON.stringify(payload.world, null, 2))

    const row = await query(
      `INSERT INTO maps (id, name, description, author_id, author_name, max_players, visible, version, blocks)
       VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
       ON CONFLICT (id) DO UPDATE
          SET name = EXCLUDED.name, description = EXCLUDED.description,
              max_players = EXCLUDED.max_players, visible = EXCLUDED.visible,
              version = EXCLUDED.version, blocks = EXCLUDED.blocks, updated_at = now()
        RETURNING *`,
      [
        payload.id,
        payload.name,
        payload.description,
        user.id,
        user.nickname,
        payload.maxPlayers,
        payload.visible,
        version,
        (payload.world as { blocks: unknown[] }).blocks.length,
      ],
    )
    sendJson(ctx.res, 201, { map: toMapInfo(row.rows[0] as never) })
  })

  router.put(/^\/api\/corner\/maps\/([\w-]+)$/, async (ctx) => {
    const user = await guard(ctx)
    const id = parseMapId(ctx.params[0])
    const existing = await findMap(id)
    if (!existing) throw new HttpError(404, 'mapa não encontrado', 'not_found')
    if (existing.author_id !== user.id) throw new HttpError(403, 'esse mapa não é seu', 'forbidden')

    const payload = parseMapPublish({ ...((await ctx.body<Record<string, unknown>>(8 * 1024 * 1024))), id })
    const version = mapVersion(payload.world as { blocks: unknown })
    await writeFile(path.join(config.worldsDir, `${id}.loom`), JSON.stringify(payload.world, null, 2))
    const row = await query(
      `UPDATE maps SET name = $2, description = $3, max_players = $4, visible = $5,
                       version = $6, blocks = $7, updated_at = now()
        WHERE id = $1 RETURNING *`,
      [id, payload.name, payload.description, payload.maxPlayers, payload.visible, version, (payload.world as { blocks: unknown[] }).blocks.length],
    )
    sendJson(ctx.res, 200, { map: toMapInfo(row.rows[0] as never) })
  })

  router.delete(/^\/api\/corner\/maps\/([\w-]+)$/, async (ctx) => {
    const user = await guard(ctx)
    const id = parseMapId(ctx.params[0])
    const existing = await findMap(id)
    if (!existing) throw new HttpError(404, 'mapa não encontrado', 'not_found')
    if (existing.author_id !== user.id) throw new HttpError(403, 'esse mapa não é seu', 'forbidden')
    await query('DELETE FROM maps WHERE id = $1', [id])
    await ctx.res.setHeader('content-length', '0')
    sendJson(ctx.res, 200, { ok: true })
  })

  // ------------------------------------------------------------------ entrar no mapa

  router.post(/^\/api\/auth\/launch$/, async (ctx) => handleLaunch(ctx, true))
  router.post(/^\/api\/maps\/([\w-]+)\/launch$/, async (ctx) => handleLaunch(ctx, true, parseMapId(ctx.params[0])))

  /** Alias antigo: exige login também (o produto pede conta para jogar). */
  router.post(/^\/api\/launch$/, async (ctx) => handleLaunch(ctx, true))

  async function handleLaunch(ctx: RequestContext, authenticated: boolean, forcedId?: string): Promise<void> {
    const user = authenticated ? await guard(ctx) : null
    const wait = limiter.take(`launch:${ctx.ip}`, 30, 60_000)
    if (wait > 0) throw new HttpError(429, `muitos lançamentos, tente em ${wait}s`, 'rate_limited')

    const body = (await ctx.body()) as Record<string, unknown>
    const mapId = parseMapId(forcedId ?? body.mapId ?? body.map_id)
    const meta = await findMap(mapId)
    if (!meta || !meta.visible) throw new HttpError(404, 'mapa não encontrado', 'not_found')

    const avatars = user ? await listAvatars(user.id) : []
    const active = avatars.find((avatar) => avatar.isActive) ?? avatars[0]
    const requestedAvatar = body.avatarId ? String(body.avatarId) : null
    const chosen = requestedAvatar ? avatars.find((avatar) => avatar.id === requestedAvatar) : active
    if (requestedAvatar && !chosen) throw new HttpError(404, 'avatar não encontrado', 'not_found')

    const room = await rooms.allocate(mapId, meta.name)
    const playerName = user?.nickname ?? String(body.name ?? 'Loomian').slice(0, 20)
    // Cor no formato do link: sem `#` (o cliente não o espera).
    const color = chosen
      ? rgbToHex(chosen.bodyColor).replace('#', '')
      : String(body.color ?? 'eabe19').replace('#', '')
    const url = launchUrl(
      { host: room.host, port: room.port, launchToken: room.launch_token, mapId },
      { playerName, color, origin: `${ctx.req.headers['x-forwarded-proto'] ?? 'http'}://${ctx.req.headers.host}/` },
    )

    sendJson(ctx.res, 200, {
      ok: true,
      map_id: mapId,
      server: `${room.host}:${room.port}`,
      token: room.launch_token,
      launch_url: url,
      arguments: launchArguments(url),
      avatar: chosen ?? null,
      message: `Sala de "${meta.name}" pronta em ${room.host}:${room.port}`,
    })
  }

  // ------------------------------------------------------------------ download do cliente

  router.get(/^\/api\/client\/download$/, async (ctx) => {
    const candidates = [
      process.env.LOOMIA_APPIMAGE_PATH,
      path.resolve(config.frontendDir, '..', 'dist', 'Loomia-x86_64.AppImage'),
      path.resolve(config.frontendDir, '..', '..', 'dist', 'Loomia-x86_64.AppImage'),
      path.resolve(process.cwd(), 'dist', 'Loomia-x86_64.AppImage'),
    ].filter((entry): entry is string => Boolean(entry))

    for (const candidate of candidates) {
      try {
        await access(candidate)
        const stat = await readFile(candidate)
        ctx.res.writeHead(200, {
          'content-type': 'application/vnd.appimage',
          'content-disposition': 'attachment; filename="Loomia-x86_64.AppImage"',
          'content-length': stat.length,
        })
        ctx.res.end(stat)
        return
      } catch {
        // tenta o próximo
      }
    }
    throw new HttpError(404, 'cliente ainda não disponível para download', 'client_missing')
  })

  router.get(/^\/api\/client\/info$/, (ctx) => {
    sendJson(ctx.res, 200, {
      name: 'Loomia',
      platform: 'linux-x86_64',
      format: 'AppImage',
      protocol: 'loomia://',
      ...inspectProtocolHandler(),
      notes: 'Baixe, dê permissão de execução (chmod +x) e abra uma vez para registrar o loomia://.',
    })
  })

  // ------------------------------------------------------------------ interno (salas)

  router.post(/^\/api\/internal\/rooms\/report$/, async (ctx) => {
    const body = (await ctx.body()) as Record<string, unknown>
    if (body.secret !== config.roomSecret) throw new HttpError(403, 'segredo inválido', 'forbidden')
    const room = rooms.report({
      session: Number(body.session),
      port: Number(body.port),
      players: Array.isArray(body.players) ? (body.players as string[]) : [],
      chat: (body.chat ?? null) as { from: string; text: string } | null,
    })
    if (!room) throw new HttpError(404, 'sala desconhecida', 'not_found')
    sendJson(ctx.res, 200, { ok: true })
  })

  router.post(/^\/api\/internal\/rooms\/close$/, async (ctx) => {
    const body = (await ctx.body()) as Record<string, unknown>
    if (body.secret !== config.roomSecret) throw new HttpError(403, 'segredo inválido', 'forbidden')
    await rooms.close(Number(body.session))
    sendJson(ctx.res, 200, { ok: true })
  })

  // Painel de administração e recursos sociais.
  createAdminApi(router)
  createSocialApi(router, guard)

  router.get(/^\/api\/launch\/info$/, (ctx) => {
    sendJson(ctx.res, 200, {
      scheme: 'loomia://',
      ...inspectProtocolHandler(),
      client: config.clientBin,
      platform: process.platform,
    })
  })

  return router
}

/** Lê o mapa do disco (404 se o arquivo não existir mais). */
export async function readWorld(config: Config, id: string): Promise<Record<string, unknown>> {
  try {
    const raw = await readFile(path.join(config.worldsDir, `${id}.loom`), 'utf8')
    return JSON.parse(raw)
  } catch {
    throw new HttpError(404, 'arquivo do mapa não encontrado', 'not_found')
  }
}

/** Mapas em disco (usado pelo seed e pela listagem de diagnóstico). */
export async function listWorldFiles(config: Config): Promise<string[]> {
  try {
    return (await readdir(config.worldsDir)).filter((file) => file.endsWith('.loom')).map((file) => file.slice(0, -5))
  } catch {
    return []
  }
}

export function newRequestId(): string {
  return randomUUID()
}

export type { ServerResponse }
export { toAvatar }
