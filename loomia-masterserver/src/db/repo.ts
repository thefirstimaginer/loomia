// Repositório de dados: contas, avatares e mapas.

import { one, query, transaction } from '../db/index.js'
import { HttpError } from '../http.js'

export interface RawUserRow {
  id: string
  first_name: string
  last_name: string
  nickname: string | null
  email: string
  bio: string
  role: string
  status: string
  last_seen_at: Date | null
  password_hash: string
  created_at: Date
  last_login_at: Date | null
}

export interface PublicUser {
  id: string
  firstName: string
  lastName: string
  nickname: string | null
  email: string
  bio: string
  role: string
  status: string
  createdAt: string
  lastLoginAt: string | null
  lastSeenAt: string | null
}

export function toPublicUser(row: RawUserRow): PublicUser {
  return {
    id: String(row.id),
    firstName: row.first_name,
    lastName: row.last_name,
    nickname: row.nickname,
    email: row.email,
    bio: row.bio ?? '',
    role: row.role ?? 'player',
    status: row.status ?? 'active',
    createdAt: row.created_at.toISOString(),
    lastLoginAt: row.last_login_at ? row.last_login_at.toISOString() : null,
    lastSeenAt: row.last_seen_at ? row.last_seen_at.toISOString() : null,
  }
}

export async function findUserById(id: string): Promise<RawUserRow | null> {
  return one<RawUserRow>('SELECT * FROM users WHERE id = $1', [id])
}

/** Busca pelo email OU pelo apelido (o "login" aceita os dois). */
export async function findUserByLogin(login: string): Promise<RawUserRow | null> {
  // Comparação sem diferenciar maiúsculas (o banco guarda o texto como veio).
  return one<RawUserRow>(
    'SELECT * FROM users WHERE lower(email) = lower($1) OR lower(nickname) = lower($1) LIMIT 1',
    [login.trim()],
  )
}

export async function createUser(input: {
  firstName: string
  lastName: string
  nickname: string
  email: string
  passwordHash: string
}): Promise<RawUserRow> {
  try {
    const row = await one<RawUserRow>(
      `INSERT INTO users (first_name, last_name, nickname, email, password_hash)
       VALUES ($1, $2, $3, $4, $5)
       RETURNING *`,
      [input.firstName, input.lastName, input.nickname, input.email, input.passwordHash],
    )
    if (!row) throw new Error('insert sem retorno')
    return row
  } catch (error) {
    const code = (error as { code?: string }).code
    const constraint = (error as { constraint?: string }).constraint ?? ''
    if (code === '23505' || code === '23000') {
      // Índices únicos: users_email_lower_idx / users_nickname_lower_idx.
      if (constraint.includes('email')) throw new HttpError(409, 'esse email já está cadastrado', 'email_taken')
      if (constraint.includes('nickname')) throw new HttpError(409, 'esse apelido já está em uso', 'nickname_taken')
      throw new HttpError(409, 'conta já existente', 'taken')
    }
    throw error
  }
}

export async function touchLogin(userId: string): Promise<void> {
  await query('UPDATE users SET last_login_at = now(), updated_at = now() WHERE id = $1', [userId])
}

export async function updateProfile(
  userId: string,
  fields: { firstName?: string; lastName?: string },
): Promise<RawUserRow | null> {
  return one<RawUserRow>(
    `UPDATE users
        SET first_name = COALESCE($2, first_name),
            last_name  = COALESCE($3, last_name),
            updated_at = now()
      WHERE id = $1
      RETURNING *`,
    [userId, fields.firstName ?? null, fields.lastName ?? null],
  )
}

// ---------------------------------------------------------------------------
// Avatares
// ---------------------------------------------------------------------------

export interface AvatarRow {
  id: string
  user_id: string
  name: string
  body_color: number[]
  shirt_color: number[]
  pants_color: number[]
  hat: string | null
  is_active: boolean
  created_at: Date
}

export interface Avatar {
  id: string
  name: string
  bodyColor: [number, number, number]
  shirtColor: [number, number, number]
  pantsColor: [number, number, number]
  hat: 'cap' | 'top_hat' | 'propeller' | 'crown' | null
  isActive: boolean
  createdAt: string
}

const triple = (value: number[]): [number, number, number] => [value[0]!, value[1]!, value[2]!]

export function toAvatar(row: AvatarRow): Avatar {
  return {
    id: String(row.id),
    name: row.name,
    bodyColor: triple(row.body_color),
    shirtColor: triple(row.shirt_color),
    pantsColor: triple(row.pants_color),
    hat: (row.hat as Avatar['hat']) ?? null,
    isActive: row.is_active,
    createdAt: row.created_at.toISOString(),
  }
}

export async function listAvatars(userId: string): Promise<Avatar[]> {
  const rows = await query<AvatarRow>(
    'SELECT * FROM avatars WHERE user_id = $1::bigint ORDER BY is_active DESC, created_at ASC',
    [userId],
  )
  return rows.rows.map(toAvatar)
}

export async function countAvatars(userId: string): Promise<number> {
  const row = await one<{ count: string }>('SELECT count(*)::text AS count FROM avatars WHERE user_id = $1::bigint', [userId])
  return Number(row?.count ?? 0)
}

export interface AvatarInput {
  name: string
  bodyColor: [number, number, number]
  shirtColor: [number, number, number]
  pantsColor: [number, number, number]
  hat: string | null
  isActive?: boolean
}

const MAX_AVATARS = 12

export async function createAvatar(userId: string, input: AvatarInput): Promise<Avatar> {
  return transaction(async (client) => {
    const count = await client.query<{ count: string }>('SELECT count(*)::text AS count FROM avatars WHERE user_id = $1::bigint', [userId])
    if (Number(count.rows[0]?.count ?? 0) >= MAX_AVATARS) {
      throw new HttpError(409, `limite de ${MAX_AVATARS} avatares por conta atingido`, 'avatar_limit')
    }
    // Se o usuário marcar como ativo, desativa o anterior.
    if (input.isActive !== false) {
      await client.query('UPDATE avatars SET is_active = false WHERE user_id = $1::bigint', [userId])
    }
    const inserted = await client.query<AvatarRow>(
      `INSERT INTO avatars (user_id, name, body_color, shirt_color, pants_color, hat, is_active)
       VALUES ($1, $2, $3, $4, $5, $6, $7)
       RETURNING *`,
      [userId, input.name, input.bodyColor, input.shirtColor, input.pantsColor, input.hat, input.isActive !== false],
    )
    return toAvatar(inserted.rows[0]!)
  })
}

export async function updateAvatar(userId: string, avatarId: string, input: AvatarInput): Promise<Avatar | null> {
  return transaction(async (client) => {
    if (input.isActive) {
      await client.query('UPDATE avatars SET is_active = false WHERE user_id = $1::bigint', [userId])
    }
    const updated = await client.query<AvatarRow>(
      `UPDATE avatars
          SET name = $3, body_color = $4, shirt_color = $5, pants_color = $6, hat = $7,
              is_active = $8, updated_at = now()
        WHERE id = $2 AND user_id = $1::bigint
        RETURNING *`,
      [userId, avatarId, input.name, input.bodyColor, input.shirtColor, input.pantsColor, input.hat, Boolean(input.isActive)],
    )
    return updated.rows[0] ? toAvatar(updated.rows[0]) : null
  })
}

export async function activateAvatar(userId: string, avatarId: string): Promise<Avatar | null> {
  return transaction(async (client) => {
    const found = await client.query('SELECT id FROM avatars WHERE id = $1 AND user_id = $2::bigint', [avatarId, userId])
    if (found.rowCount === 0) return null
    await client.query('UPDATE avatars SET is_active = false WHERE user_id = $1::bigint', [userId])
    const updated = await client.query<AvatarRow>(
      'UPDATE avatars SET is_active = true, updated_at = now() WHERE id = $1 RETURNING *',
      [avatarId],
    )
    return updated.rows[0] ? toAvatar(updated.rows[0]) : null
  })
}

export async function deleteAvatar(userId: string, avatarId: string): Promise<boolean> {
  return transaction(async (client) => {
    const removed = await client.query<{ was_active: boolean }>(
      'DELETE FROM avatars WHERE id = $1 AND user_id = $2::bigint RETURNING is_active AS was_active',
      [avatarId, userId],
    )
    if (removed.rowCount === 0) return false
    if (removed.rows[0]?.was_active) {
      // Promove o mais antigo a ativo, para o jogador nunca ficar sem avatar.
      await client.query(
        `UPDATE avatars SET is_active = true
          WHERE id = (SELECT id FROM avatars WHERE user_id = $1::bigint ORDER BY created_at ASC LIMIT 1)`,
        [userId],
      )
    }
    return true
  })
}

// ---------------------------------------------------------------------------
// Mapas
// ---------------------------------------------------------------------------

export interface MapRow {
  id: string
  name: string
  description: string
  author_id: string | null
  author_name: string
  max_players: number
  visible: boolean
  version: string
  blocks: number
  created_at: Date
  updated_at: Date
}

export interface MapInfo {
  id: string
  name: string
  description: string
  author: string
  authorId: string | null
  maxPlayers: number
  blocks: number
  version: string
  visibility: 'public' | 'private'
  createdAt: string
  updatedAt: string
}

export function toMapInfo(row: MapRow): MapInfo {
  return {
    id: row.id,
    name: row.name,
    description: row.description,
    author: row.author_name,
    authorId: row.author_id ? String(row.author_id) : null,
    maxPlayers: row.max_players,
    blocks: row.blocks,
    version: row.version,
    visibility: row.visible ? 'public' : 'private',
    createdAt: row.created_at.toISOString(),
    updatedAt: row.updated_at.toISOString(),
  }
}

export async function listPublicMaps(search: string, limit = 60): Promise<MapInfo[]> {
  const term = `%${search.trim()}%`
  const rows = await query<MapRow>(
    `SELECT * FROM maps
      WHERE visible = true
        AND ($1 = '%%' OR name ILIKE $1 OR description ILIKE $1 OR author_name ILIKE $1)
      ORDER BY updated_at DESC
      LIMIT $2`,
    [term || '%%', limit],
  )
  return rows.rows.map(toMapInfo)
}

export async function listMapsByAuthor(userId: string): Promise<MapInfo[]> {
  const rows = await query<MapRow>('SELECT * FROM maps WHERE author_id = $1 ORDER BY updated_at DESC', [userId])
  return rows.rows.map(toMapInfo)
}

export async function findMap(id: string): Promise<MapRow | null> {
  return one<MapRow>('SELECT * FROM maps WHERE id = $1', [id])
}
