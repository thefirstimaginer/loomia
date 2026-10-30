// Validação de entrada (sem dependências externas).

import { HttpError } from './http.js'

const EMAIL = /^[^\s@]+@[^\s@]+\.[^\s@]{2,}$/
const NICKNAME = /^[a-zA-Z0-9_.-]{3,20}$/
const MAP_ID = /^[a-z0-9][a-z0-9-]{1,39}$/
const HATS = ['cap', 'top_hat', 'propeller', 'crown'] as const

export type Hat = (typeof HATS)[number] | null

function text(value: unknown): string {
  return typeof value === 'string' ? value.trim() : ''
}

function required(value: unknown, field: string, min: number, max: number): string {
  const result = text(value)
  if (result.length < min) throw new HttpError(400, `${field} precisa ter ao menos ${min} caractere(s)`, 'invalid')
  if (result.length > max) throw new HttpError(400, `${field} pode ter no máximo ${max} caracteres`, 'invalid')
  // Rejeita caracteres de controle.
  if (/[\u0000-\u001f\u007f]/.test(result)) throw new HttpError(400, `${field} tem caracteres inválidos`, 'invalid')
  return result
}

export interface RegisterInput {
  firstName: string
  lastName: string
  nickname: string
  email: string
  password: string
}

export function parseRegister(body: unknown): RegisterInput {
  const data = (body ?? {}) as Record<string, unknown>
  const firstName = required(data.firstName ?? data.first_name ?? data.nome, 'Nome', 1, 60)
  const lastName = required(data.lastName ?? data.last_name ?? data.sobrenome, 'Sobrenome', 1, 60)
  const nickname = required(data.nickname ?? data.apelido, 'Apelido', 3, 20)
  if (!NICKNAME.test(nickname)) {
    throw new HttpError(400, 'o apelido usa letras, números, ponto, hífen e _ (3–20)', 'invalid_nickname')
  }
  const email = required(data.email, 'Email', 5, 254).toLowerCase()
  if (!EMAIL.test(email)) throw new HttpError(400, 'email inválido', 'invalid_email')

  const password = typeof data.password === 'string' ? data.password : ''
  const confirm = typeof (data.passwordConfirm ?? data.password_confirm ?? data.repeatPassword) === 'string'
    ? String(data.passwordConfirm ?? data.password_confirm ?? data.repeatPassword)
    : password
  if (password.length < 8) throw new HttpError(400, 'a senha precisa ter ao menos 8 caracteres', 'weak_password')
  if (password.length > 200) throw new HttpError(400, 'a senha é longa demais', 'invalid')
  if (password !== confirm) throw new HttpError(400, 'as senhas não coincidem', 'password_mismatch')

  return { firstName, lastName, nickname, email, password }
}

export function parseLogin(body: unknown): { login: string; password: string } {
  const data = (body ?? {}) as Record<string, unknown>
  const login = required(data.login ?? data.email ?? data.nickname, 'Email ou apelido', 3, 254)
  const password = typeof data.password === 'string' ? data.password : ''
  if (!password) throw new HttpError(400, 'informe a senha', 'invalid')
  return { login, password }
}

function color(value: unknown, field: string): [number, number, number] {
  if (!Array.isArray(value) || value.length !== 3) {
    throw new HttpError(400, `${field} precisa ser [r, g, b]`, 'invalid_avatar')
  }
  return value.map((entry) => {
    const number = Number(entry)
    if (!Number.isFinite(number)) throw new HttpError(400, `${field} inválido`, 'invalid_avatar')
    return Math.max(0, Math.min(255, Math.round(number)))
  }) as [number, number, number]
}

export interface AvatarPayload {
  name: string
  bodyColor: [number, number, number]
  shirtColor: [number, number, number]
  pantsColor: [number, number, number]
  hat: Hat
  isActive: boolean
}

export function parseAvatar(body: unknown): AvatarPayload {
  const data = (body ?? {}) as Record<string, unknown>
  const name = required(data.name ?? 'Meu avatar', 'Nome do avatar', 1, 40)
  const hatValue = data.hat
  const hat = hatValue === null || hatValue === undefined || hatValue === ''
    ? null
    : (HATS as readonly string[]).includes(String(hatValue))
      ? (String(hatValue) as Hat)
      : (() => {
        throw new HttpError(400, 'chapéu desconhecido', 'invalid_avatar')
      })()
  return {
    name,
    bodyColor: color(data.bodyColor ?? data.body_color, 'cor da pele'),
    shirtColor: color(data.shirtColor ?? data.shirt_color, 'cor da camisa'),
    pantsColor: color(data.pantsColor ?? data.pants_color, 'cor da calça'),
    hat,
    isActive: data.isActive !== false && data.is_active !== false,
  }
}

export function parseMapId(value: unknown): string {
  const id = text(value).toLowerCase()
  if (!MAP_ID.test(id)) throw new HttpError(400, 'id de mapa inválido', 'invalid_map')
  return id
}

export interface MapPublishPayload {
  id: string
  name: string
  description: string
  maxPlayers: number
  visible: boolean
  world: unknown
}

export function parseMapPublish(body: unknown): MapPublishPayload {
  const data = (body ?? {}) as Record<string, unknown>
  const world = (data.world ?? data.map ?? data) as Record<string, unknown>
  const id = parseMapId(data.id ?? world.id)
  if (!data.world && !data.map && !Array.isArray(world.blocks)) {
    throw new HttpError(400, 'envie {"world": {"id": "...", "blocks": [...]}}', 'invalid_map')
  }
  const worldRecord = world as { blocks?: unknown; spawn?: unknown; sky_color?: unknown }
  if (!Array.isArray(worldRecord.blocks) || worldRecord.blocks.length === 0) {
    throw new HttpError(400, 'o mapa precisa ter blocos', 'invalid_map')
  }
  if (worldRecord.blocks.length > 200_000) {
    throw new HttpError(413, 'mapa com blocos demais (limite 200 mil)', 'invalid_map')
  }
  const maxPlayers = Math.round(Number(data.maxPlayers ?? data.max_players ?? 32))
  if (!Number.isFinite(maxPlayers) || maxPlayers < 1 || maxPlayers > 32) {
    throw new HttpError(400, 'maxPlayers deve ser entre 1 e 32', 'invalid_map')
  }
  return {
    id,
    name: required(data.name ?? world.name ?? id, 'Nome do mapa', 2, 60),
    description: text(data.description ?? world.description).slice(0, 400),
    maxPlayers,
    visible: data.visible !== false,
    world: {
      ...worldRecord,
      id,
      name: data.name ?? world.name ?? id,
      description: data.description ?? world.description ?? '',
      spawn: worldRecord.spawn ?? [0, 1, 4],
      sky_color: worldRecord.sky_color ?? [150, 200, 240],
    },
  }
}

// ---------------------------------------------------------------------------
// Helpers compartilhados pelas rotas (admin, comunidade, social)
// ---------------------------------------------------------------------------

/** Texto opcional: trim, exigência de tamanho mínimo e limite máximo. */
export function parseText(value: unknown, field: string, min: number, max: number): string {
  const result = typeof value === 'string' ? value.trim() : ''
  if (result.length < min) throw new HttpError(400, `${field} é obrigatório`, 'invalid')
  if (result.length > max) throw new HttpError(400, `${field} é longo demais`, 'invalid')
  return result
}

/** Id numérico vindo da URL/corpo, normalizado como string decimal. */
export function parseId(value: string | undefined, field = 'id'): string {
  const id = Number(value)
  if (!Number.isInteger(id) || id <= 0) throw new HttpError(400, `${field} inválido`, 'invalid')
  return String(id)
}

/** Lê um valor de uma lista fechada; devolve null quando não é válido. */
export function parseEnum<T extends string>(value: unknown, allowed: readonly T[]): T | null {
  const result = typeof value === 'string' ? value : ''
  return (allowed as readonly string[]).includes(result) ? (result as T) : null
}
