// Recursos sociais do site: perfil público, favoritos, amizades e mensagens.

import { HttpError } from '../http.js'
import { one, query, transaction } from '../db/index.js'
import { toAvatar, toMapInfo } from './repo.js'
import type { Avatar, AvatarRow, MapInfo, MapRow } from './repo.js'

export interface PublicProfile {
  id: string
  firstName: string
  lastName: string
  nickname: string | null
  bio: string
  createdAt: string
  isMe: boolean
  isFriend: boolean
  hasPendingRequest: boolean
  pendingRequestFromMe: boolean
  isFavorite: boolean
  mapsCount: number
  maps: MapInfo[]
  avatars: Avatar[]
  friends: number
  favorites: number
}

export async function publicProfile(viewerId: string, targetId: string): Promise<PublicProfile | null> {
  const user = await one<Record<string, unknown>>(
    `SELECT id, first_name, last_name, nickname, bio, created_at, status
       FROM users WHERE id = $1`,
    [targetId],
  )
  if (!user || user.status === 'banned') return null

  const [avatars, maps, relation, counts] = await Promise.all([
    query<AvatarRow>('SELECT * FROM avatars WHERE user_id = $1::bigint ORDER BY is_active DESC, created_at ASC', [targetId]),
    query<MapRow>('SELECT * FROM maps WHERE author_id = $1::bigint AND visible = true ORDER BY updated_at DESC LIMIT 24', [targetId]),
    one<Record<string, unknown>>(
      `SELECT
         EXISTS (SELECT 1 FROM friend_requests WHERE status = 'accepted'
                  AND ((from_id = $1::bigint::bigint AND to_id = $2::bigint::bigint) OR (from_id = $2::bigint::bigint AND to_id = $1::bigint::bigint))) AS is_friend,
         EXISTS (SELECT 1 FROM friend_requests WHERE status = 'pending'
                  AND ((from_id = $1::bigint::bigint AND to_id = $2::bigint::bigint) OR (from_id = $2::bigint::bigint AND to_id = $1::bigint::bigint))) AS has_pending,
         EXISTS (SELECT 1 FROM friend_requests WHERE status = 'pending' AND from_id = $1::bigint::bigint AND to_id = $2::bigint::bigint) AS pending_from_me,
         EXISTS (SELECT 1 FROM favorites WHERE user_id = $1::bigint::bigint AND target_id = $2::bigint::bigint) AS is_favorite`,
      [viewerId, targetId],
    ),
    one<{ friends: string; favorites: string }>(
      `SELECT
         (SELECT count(*) FROM friend_requests WHERE status = 'accepted'
           AND (from_id = $1::bigint OR to_id = $1::bigint)) AS friends,
         (SELECT count(*) FROM favorites WHERE user_id = $1::bigint::bigint) AS favorites`,
      [targetId],
    ),
  ])

  return {
    id: String(user.id),
    firstName: String(user.first_name),
    lastName: String(user.last_name),
    nickname: (user.nickname as string | null) ?? null,
    bio: String(user.bio ?? ''),
    createdAt: new Date(user.created_at as Date).toISOString(),
    isMe: String(user.id) === viewerId,
    isFriend: Boolean(relation?.is_friend),
    hasPendingRequest: Boolean(relation?.has_pending),
    pendingRequestFromMe: Boolean(relation?.pending_from_me),
    isFavorite: Boolean(relation?.is_favorite),
    mapsCount: maps.rowCount ?? 0,
    maps: maps.rows.map(toMapInfo),
    avatars: avatars.rows.map(toAvatar),
    friends: Number(counts?.friends ?? 0),
    favorites: Number(counts?.favorites ?? 0),
  } satisfies PublicProfile
}

export async function setFavorite(userId: string, targetId: string, favorite: boolean): Promise<void> {
  if (userId === targetId) throw new HttpError(400, 'não dá para favoritar você mesmo', 'invalid')
  const target = await one('SELECT id FROM users WHERE id = $1 AND status <> \'banned\'', [targetId])
  if (!target) throw new HttpError(404, 'jogador não encontrado', 'not_found')
  if (favorite) {
    await query('INSERT INTO favorites (user_id, target_id) VALUES ($1, $2) ON CONFLICT DO NOTHING', [userId, targetId])
  } else {
    await query('DELETE FROM favorites WHERE user_id = $1::bigint AND target_id = $2::bigint', [userId, targetId])
  }
}

export async function listFavorites(userId: string): Promise<{ id: string; nickname: string | null; bio: string }[]> {
  const rows = await query<{ id: string; nickname: string | null; bio: string }>(
    `SELECT u.id, u.nickname, u.bio
       FROM favorites f JOIN users u ON u.id = f.target_id
      WHERE f.user_id = $1::bigint::bigint AND u.status <> 'banned'
      ORDER BY f.created_at DESC`,
    [userId],
  )
  return rows.rows
}

// ---------------------------------------------------------------------------
// Amizades
// ---------------------------------------------------------------------------

export interface FriendRequest {
  id: string
  fromId: string
  fromName: string
  toId: string
  toName: string
  message: string
  status: 'pending' | 'accepted' | 'declined' | 'cancelled'
  direction: 'incoming' | 'outgoing'
  createdAt: string
}

function toFriendRequest(row: Record<string, unknown>, viewerId: string): FriendRequest {
  return {
    id: String(row.id),
    fromId: String(row.from_id),
    fromName: String(row.from_name ?? ''),
    toId: String(row.to_id),
    toName: String(row.to_name ?? ''),
    message: String(row.message ?? ''),
    status: row.status as FriendRequest['status'],
    direction: String(row.from_id) === viewerId ? 'outgoing' : 'incoming',
    createdAt: new Date(row.created_at as Date).toISOString(),
  }
}

const REQUEST_SELECT = `
  SELECT r.*, f.nickname AS from_name, t.nickname AS to_name
    FROM friend_requests r
    JOIN users f ON f.id = r.from_id
    JOIN users t ON t.id = r.to_id`

export async function sendFriendRequest(fromId: string, toId: string, message: string): Promise<FriendRequest> {
  if (fromId === toId) throw new HttpError(400, 'não dá para adicionar você mesmo', 'invalid')
  const target = await one("SELECT id FROM users WHERE id = $1::bigint AND status <> 'banned'", [toId])
  if (!target) throw new HttpError(404, 'jogador não encontrado', 'not_found')

  const existing = await one<{ status: string; id: string; from_id: string }>(
    `SELECT id, status, from_id FROM friend_requests
      WHERE status IN ('pending', 'accepted')
        AND ((from_id = $1::bigint::bigint AND to_id = $2::bigint::bigint) OR (from_id = $2::bigint::bigint AND to_id = $1::bigint::bigint))
      LIMIT 1`,
    [fromId, toId],
  )
  if (existing?.status === 'accepted') throw new HttpError(409, 'vocês já são amigos', 'already_friends')
  if (existing?.status === 'pending') {
    // Pedido cruzado: aceitar automaticamente é o comportamento esperado.
    if (existing.from_id !== fromId) return respondFriendRequest(existing.id, toId, 'accepted')
    throw new HttpError(409, 'já existe um pedido pendente', 'already_pending')
  }

  const row = await one(
    `INSERT INTO friend_requests (from_id, to_id, message) VALUES ($1, $2, $3) RETURNING id`,
    [fromId, toId, message.slice(0, 500)],
  )
  const full = await one(`${REQUEST_SELECT} WHERE r.id = $1`, [row!.id])
  return toFriendRequest(full!, fromId)
}

export async function respondFriendRequest(
  requestId: string,
  userId: string,
  action: 'accepted' | 'declined' | 'cancelled',
): Promise<FriendRequest> {
  return transaction(async (client) => {
    const found = await client.query<{ from_id: string; to_id: string; status: string }>(
      'SELECT from_id, to_id, status FROM friend_requests WHERE id = $1 FOR UPDATE',
      [requestId],
    )
    if (found.rowCount === 0) throw new HttpError(404, 'pedido não encontrado', 'not_found')
    const request = found.rows[0]!
    if (request.status !== 'pending') throw new HttpError(409, 'esse pedido já foi respondido', 'already_answered')
    // Aceitar/recusar: só quem recebeu. Cancelar: só quem enviou.
    if (action === 'cancelled' ? request.from_id !== userId : request.to_id !== userId) {
      throw new HttpError(403, 'esse pedido não é seu', 'forbidden')
    }
    await client.query(
      'UPDATE friend_requests SET status = $2, responded_at = now() WHERE id = $1',
      [requestId, action],
    )
    const row = await client.query(`${REQUEST_SELECT} WHERE r.id = $1`, [requestId])
    return toFriendRequest(row.rows[0]!, userId)
  })
}

export async function listFriendRequests(userId: string): Promise<FriendRequest[]> {
  const rows = await query(
    `${REQUEST_SELECT}
      WHERE (r.to_id = $1::bigint::bigint OR r.from_id = $1::bigint::bigint)
      ORDER BY (r.status = 'pending') DESC, r.created_at DESC LIMIT 100`,
    [userId],
  )
  return rows.rows.map((row) => toFriendRequest(row, userId))
}

export interface Friend {
  id: string
  nickname: string | null
  bio: string
}

export async function listFriends(userId: string): Promise<Friend[]> {
  const rows = await query<Friend>(
    `SELECT u.id, u.nickname, u.bio
       FROM friend_requests r
       JOIN users u ON u.id = CASE WHEN r.from_id = $1::bigint::bigint THEN r.to_id ELSE r.from_id END
      WHERE r.status = 'accepted' AND (r.from_id = $1::bigint::bigint OR r.to_id = $1::bigint::bigint)
      ORDER BY u.nickname`,
    [userId],
  )
  return rows.rows
}

export async function areFriends(a: string, b: string): Promise<boolean> {
  const row = await one(
    `SELECT 1 FROM friend_requests
      WHERE status = 'accepted'
        AND ((from_id = $1::bigint::bigint AND to_id = $2::bigint::bigint) OR (from_id = $2::bigint::bigint AND to_id = $1::bigint::bigint))`,
    [a, b],
  )
  return Boolean(row)
}

// ---------------------------------------------------------------------------
// Mensagens
// ---------------------------------------------------------------------------

export interface Message {
  id: string
  fromId: string
  toId: string
  body: string
  createdAt: string
  read: boolean
}

function toMessage(row: Record<string, unknown>): Message {
  return {
    id: String(row.id),
    fromId: String(row.from_id),
    toId: String(row.to_id),
    body: String(row.body),
    createdAt: new Date(row.created_at as Date).toISOString(),
    read: Boolean(row.read_at),
  }
}

/**
 * Mensagem entre jogadores. Antes do aceite, ela só é permitida pelo pedido de
 * amizade (campo `message`). Depois de amigos, é uma conversa normal.
 */
export async function sendMessage(fromId: string, toId: string, body: string): Promise<Message> {
  if (fromId === toId) throw new HttpError(400, 'não dá para mandar mensagem para você mesmo', 'invalid')
  if (!(await areFriends(fromId, toId))) {
    throw new HttpError(403, 'vocês precisam ser amigos para trocar mensagens', 'not_friends')
  }
  const row = await one(
    'INSERT INTO messages (from_id, to_id, body) VALUES ($1, $2, $3) RETURNING *',
    [fromId, toId, body.slice(0, 1000)],
  )
  return toMessage(row!)
}

export async function listMessages(userId: string, otherId: string): Promise<Message[]> {
  const rows = await query(
    `SELECT * FROM messages
      WHERE (from_id = $1::bigint::bigint AND to_id = $2::bigint::bigint) OR (from_id = $2::bigint::bigint AND to_id = $1::bigint::bigint)
      ORDER BY created_at ASC LIMIT 200`,
    [userId, otherId],
  )
  await query('UPDATE messages SET read_at = now() WHERE from_id = $2::bigint::bigint AND to_id = $1::bigint::bigint AND read_at IS NULL', [
    userId,
    otherId,
  ])
  return rows.rows.map(toMessage)
}

export async function unreadCount(userId: string): Promise<number> {
  const row = await one<{ count: string }>(
    'SELECT count(*)::text AS count FROM messages WHERE to_id = $1::bigint::bigint AND read_at IS NULL',
    [userId],
  )
  return Number(row?.count ?? 0)
}

export async function updateBio(userId: string, bio: string): Promise<void> {
  await query('UPDATE users SET bio = $2, updated_at = now() WHERE id = $1', [userId, bio.slice(0, 500)])
}
