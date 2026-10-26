// Dados de comunidade: visitas/estrelas de mapas, fórum e blog.
//
// Consultas pequenas e diretas — o mesmo estilo dos outros módulos de db.

import { one, query, transaction } from './index.js'
import type { Avatar, AvatarRow, MapInfo, MapRow } from './repo.js'
import { toAvatar, toMapInfo } from './repo.js'

const triple = (value: number[]): [number, number, number] => [value[0]!, value[1]!, value[2]!]

function toAvatarNullable(row: AvatarRow | undefined): Avatar | null {
  return row ? toAvatar(row) : null
}

// ---------------------------------------------------------------- mundo

export interface MapDetail extends MapInfo {
  visits: number
  stars: number
  starCount: number
  authorNickname: string | null
  authorAvatar: Avatar | null
  /** Se o jogador que pediu já deu estrela a este mapa. */
  starred?: boolean
}

export async function mapDetail(mapId: string, viewerId?: string): Promise<MapDetail | null> {
  const meta = await one<MapRow & { author_nickname: string | null }>(
    `SELECT m.*, u.nickname AS author_nickname
       FROM maps m LEFT JOIN users u ON u.id = m.author_id
      WHERE m.id = $1`,
    [mapId],
  )
  if (!meta) return null

  const [stats, avatarRow] = await Promise.all([
    one<{ visits: string; stars: string; star_count: string }>(
      `SELECT
         (SELECT count(*) FROM map_visits WHERE map_id = $1) AS visits,
         (SELECT count(*) FROM map_stars WHERE map_id = $1) AS stars,
         (SELECT count(*) FROM map_stars WHERE map_id = $1) AS star_count`,
      [mapId],
    ),
    meta.author_id
      ? one<AvatarRow & { user_id: string }>(
          `SELECT * FROM avatars WHERE user_id = $1::bigint ORDER BY is_active DESC, created_at ASC LIMIT 1`,
          [meta.author_id],
        )
      : Promise.resolve(null),
  ])

  let starred = false
  if (viewerId) {
    const found = await one<{ user_id: string }>('SELECT user_id FROM map_stars WHERE map_id = $1 AND user_id = $2::bigint', [
      mapId,
      viewerId,
    ])
    starred = Boolean(found)
  }

  return {
    ...toMapInfo(meta),
    visits: Number(stats?.visits ?? 0),
    stars: Number(stats?.stars ?? 0),
    starCount: Number(stats?.star_count ?? 0),
    authorNickname: meta.author_nickname ?? null,
    authorAvatar: toAvatarNullable(avatarRow ?? undefined),
    starred,
  }
}

/** Registra uma visita (chamado no launch do mapa). */
export async function recordVisit(mapId: string, userId: string | null): Promise<void> {
  await query('INSERT INTO map_visits (map_id, user_id) VALUES ($1, $2::bigint)', [mapId, userId])
}

/** Alterna a estrela do jogador no mapa. Devolve o total e se ficou marcada. */
export async function toggleStar(mapId: string, userId: string): Promise<{ stars: number; starred: boolean }> {
  return transaction(async (client) => {
    const removed = await client.query('DELETE FROM map_stars WHERE map_id = $1 AND user_id = $2::bigint RETURNING user_id', [
      mapId,
      userId,
    ])
    if (removed.rowCount === 0) {
      await client.query('INSERT INTO map_stars (map_id, user_id) VALUES ($1, $2::bigint) ON CONFLICT DO NOTHING', [mapId, userId])
    }
    const total = await client.query<{ count: string }>('SELECT count(*)::text AS count FROM map_stars WHERE map_id = $1', [mapId])
    const starred = removed.rowCount === 0
    return { stars: Number(total.rows[0]?.count ?? 0), starred }
  })
}

// ---------------------------------------------------------------- fórum

export interface ForumRoom {
  id: string
  title: string
  description: string
  authorId: string | null
  authorName: string
  replies: number
  lastActivityAt: string
  createdAt: string
}

interface ForumRoomRow {
  id: string
  author_id: string | null
  author_name: string
  title: string
  description: string
  created_at: Date
  last_activity: Date
  replies: string
}

function toRoom(row: ForumRoomRow): ForumRoom {
  return {
    id: String(row.id),
    title: row.title,
    description: row.description,
    authorId: row.author_id ? String(row.author_id) : null,
    authorName: row.author_name,
    replies: Number(row.replies),
    lastActivityAt: row.last_activity.toISOString(),
    createdAt: row.created_at.toISOString(),
  }
}

export async function listForumRooms(limit = 50): Promise<ForumRoom[]> {
  const rows = await query<ForumRoomRow>(
    `SELECT r.*, (SELECT count(*) FROM forum_posts p WHERE p.room_id = r.id) AS replies
       FROM forum_rooms r ORDER BY r.last_activity DESC LIMIT $1`,
    [limit],
  )
  return rows.rows.map(toRoom)
}

export async function createForumRoom(input: {
  authorId: string
  authorName: string
  title: string
  description: string
}): Promise<ForumRoom> {
  const row = await one<ForumRoomRow & { replies: string }>(
    `INSERT INTO forum_rooms (author_id, author_name, title, description)
     VALUES ($1::bigint, $2, $3, $4)
     RETURNING *, 0 AS replies`,
    [input.authorId, input.authorName, input.title, input.description],
  )
  return toRoom(row!)
}

export async function findForumRoom(id: number): Promise<ForumRoom | null> {
  const row = await one<ForumRoomRow>(
    `SELECT r.*, (SELECT count(*) FROM forum_posts p WHERE p.room_id = r.id) AS replies
       FROM forum_rooms r WHERE r.id = $1`,
    [id],
  )
  return row ? toRoom(row) : null
}

export interface ForumPost {
  id: string
  roomId: string
  authorId: string | null
  authorName: string
  authorAvatar: Avatar | null
  body: string
  createdAt: string
}

export async function listForumPosts(roomId: number, limit = 200): Promise<ForumPost[]> {
  const rows = await query<
    {
      id: string
      room_id: string
      author_id: string | null
      author_name: string
      body: string
      created_at: Date
      body_color: number[]
      shirt_color: number[]
      pants_color: number[]
      hat: string | null
      is_active: boolean
      avatar_created_at: Date | null
      avatar_id: string | null
    } & Record<string, unknown>
  >(
    `SELECT p.id, p.room_id, p.author_id, p.author_name, p.body, p.created_at,
            a.id AS avatar_id, a.body_color, a.shirt_color, a.pants_color, a.hat, a.is_active, a.created_at AS avatar_created_at
       FROM forum_posts p
       LEFT JOIN avatars a ON a.user_id = p.author_id AND a.is_active
      WHERE p.room_id = $1
      ORDER BY p.created_at ASC LIMIT $2`,
    [roomId, limit],
  )
  return rows.rows.map((row) => ({
    id: String(row.id),
    roomId: String(row.room_id),
    authorId: row.author_id ? String(row.author_id) : null,
    authorName: row.author_name,
    body: row.body,
    createdAt: (row.created_at as Date).toISOString(),
    authorAvatar:
      row.avatar_id && row.body_color
        ? {
            id: String(row.avatar_id),
            name: '',
            bodyColor: triple(row.body_color ?? []),
            shirtColor: triple(row.shirt_color ?? []),
            pantsColor: triple(row.pants_color ?? []),
            hat: (row.hat as Avatar['hat']) ?? null,
            isActive: Boolean(row.is_active),
            createdAt: (row.avatar_created_at as Date).toISOString(),
          }
        : null,
  }))
}

export async function createForumPost(input: {
  roomId: number
  authorId: string
  authorName: string
  body: string
}): Promise<ForumPost> {
  return transaction(async (client) => {
    const inserted = await client.query<{ id: string; created_at: Date }>(
      `INSERT INTO forum_posts (room_id, author_id, author_name, body)
       VALUES ($1, $2::bigint, $3, $4)
       RETURNING id, created_at`,
      [input.roomId, input.authorId, input.authorName, input.body],
    )
    await client.query('UPDATE forum_rooms SET last_activity = now() WHERE id = $1', [input.roomId])
    const row = inserted.rows[0]!
    return {
      id: String(row.id),
      roomId: String(input.roomId),
      authorId: input.authorId,
      authorName: input.authorName,
      body: input.body,
      authorAvatar: null,
      createdAt: row.created_at.toISOString(),
    }
  })
}

// ---------------------------------------------------------------- blog

export interface BlogPost {
  id: string
  slug: string
  title: string
  summary: string
  body: string
  author: string
  tag: string
  createdAt: string
}

interface BlogRow {
  id: string
  slug: string
  author: string
  title: string
  tag: string
  summary: string
  body: string
  created_at: Date
}

function toBlog(row: BlogRow): BlogPost {
  return {
    id: String(row.id),
    slug: row.slug,
    title: row.title,
    summary: row.summary,
    body: row.body,
    author: row.author,
    tag: row.tag,
    createdAt: row.created_at.toISOString(),
  }
}

export async function listBlogPosts(limit = 30): Promise<BlogPost[]> {
  const rows = await query<BlogRow>('SELECT * FROM blog_posts ORDER BY created_at DESC LIMIT $1', [limit])
  return rows.rows.map(toBlog)
}

export async function findBlogPost(slug: string): Promise<BlogPost | null> {
  const row = await one<BlogRow>('SELECT * FROM blog_posts WHERE slug = $1', [slug])
  return row ? toBlog(row) : null
}

/** Cria/atualiza uma notícia (usado pelo painel admin). */
export async function upsertBlogPost(input: {
  slug: string
  author: string
  title: string
  tag: string
  summary: string
  body: string
}): Promise<BlogPost> {
  const row = await one<BlogRow>(
    `INSERT INTO blog_posts (slug, author, title, tag, summary, body)
     VALUES ($1, $2, $3, $4, $5, $6)
     ON CONFLICT (slug) DO UPDATE
        SET author = EXCLUDED.author, title = EXCLUDED.title, tag = EXCLUDED.tag,
            summary = EXCLUDED.summary, body = EXCLUDED.body, updated_at = now()
     RETURNING *`,
    [input.slug, input.author, input.title, input.tag, input.summary, input.body],
  )
  return toBlog(row!)
}

// ---------------------------------------------------------------- rede

export interface NetworkPlayer {
  id: string
  nickname: string | null
  bio: string
  online: boolean
  avatar: Avatar | null
}

/** Todos os jogadores da rede (para a aba Amigos), com o avatar ativo. */
export async function listNetworkPlayers(limit = 200): Promise<NetworkPlayer[]> {
  const rows = await query<
    {
      id: string
      nickname: string | null
      bio: string
      online: boolean
      avatar_id: string | null
      body_color: number[] | null
      shirt_color: number[] | null
      pants_color: number[] | null
      hat: string | null
      is_active: boolean | null
      avatar_created_at: Date | null
    } & Record<string, unknown>
  >(
    `SELECT u.id, u.nickname, u.bio,
            (u.last_seen_at IS NOT NULL AND u.last_seen_at > now() - interval '5 minutes') AS online,
            a.id AS avatar_id, a.body_color, a.shirt_color, a.pants_color, a.hat, a.is_active, a.created_at AS avatar_created_at
       FROM users u
       LEFT JOIN avatars a ON a.user_id = u.id AND a.is_active
      WHERE u.status <> 'banned' AND u.nickname IS NOT NULL AND u.role = 'player'
      ORDER BY online DESC, u.nickname
      LIMIT $1`,
    [limit],
  )
  return rows.rows.map((row) => ({
    id: String(row.id),
    nickname: row.nickname,
    bio: row.bio ?? '',
    online: Boolean(row.online),
    avatar:
      row.avatar_id && row.body_color
        ? {
            id: String(row.avatar_id),
            name: '',
            bodyColor: triple(row.body_color ?? []),
            shirtColor: triple(row.shirt_color ?? []),
            pantsColor: triple(row.pants_color ?? []),
            hat: (row.hat as Avatar['hat']) ?? null,
            isActive: Boolean(row.is_active),
            createdAt: (row.avatar_created_at as Date).toISOString(),
          }
        : null,
  }))
}
