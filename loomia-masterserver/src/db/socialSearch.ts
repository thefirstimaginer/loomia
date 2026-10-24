// Busca de jogadores (usada pelo site do jogador e pelas rotas sociais).

import { query } from './index.js'

export interface PlayerSummary {
  id: string
  nickname: string | null
  bio: string
  online: boolean
}

/** Busca jogadores pelo apelido/nome, opcionalmente excluindo quem já é favorito. */
export async function listFavoriteSafe(term: string, limit = 30, viewerId?: string): Promise<PlayerSummary[]> {
  if (viewerId) {
    const rows = await query<PlayerSummary & { online: boolean }>(
      `SELECT u.id, u.nickname, u.bio,
              (u.last_seen_at IS NOT NULL AND u.last_seen_at > now() - interval '5 minutes') AS online
         FROM favorites f JOIN users u ON u.id = f.target_id
        WHERE f.user_id = $1::bigint AND u.status <> 'banned'
        ORDER BY f.created_at DESC LIMIT $2`,
      [viewerId, limit],
    )
    return rows.rows
  }

  const like = `%${term}%`
  const rows = await query<PlayerSummary>(
    `SELECT u.id, u.nickname, u.bio,
            (u.last_seen_at IS NOT NULL AND u.last_seen_at > now() - interval '5 minutes') AS online
       FROM users u
      WHERE u.status <> 'banned' AND u.nickname IS NOT NULL
        AND ($1 = '%%' OR u.nickname ILIKE $1 OR u.first_name ILIKE $1)
      ORDER BY (u.last_seen_at > now() - interval '5 minutes') DESC, u.nickname
      LIMIT $2`,
    [like || '%%', limit],
  )
  return rows.rows
}

