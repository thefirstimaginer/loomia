// Utilidades comuns: JSON, HTTP, ids e criptografia (somente módulos nativos).

import crypto from 'node:crypto'
import fs from 'node:fs/promises'
import path from 'node:path'

export const now = () => Date.now()

export function randomToken(bytes = 18) {
  return crypto.randomBytes(bytes).toString('base64url')
}

export async function readJson(file, fallback) {
  try {
    return JSON.parse(await fs.readFile(file, 'utf8'), reviveArrays)
  } catch {
    return fallback
  }
}

/**
 * Grava de forma atômica (arquivo temporário + rename) para nunca deixar um
 * JSON pela metade se o processo morrer no meio da escrita.
 */
export async function writeJson(file, value) {
  await fs.mkdir(path.dirname(file), { recursive: true })
  const tmp = `${file}.${process.pid}.${randomToken(4)}.tmp`
  await fs.writeFile(tmp, JSON.stringify(value, null, 2))
  await fs.rename(tmp, file)
}

/** JSON com chaves em ordem — evita "expiração" de cache por reordenação. */
export function stableJson(value) {
  return JSON.stringify(value, (_key, entry) => {
    if (entry && typeof entry === 'object' && !Array.isArray(entry)) {
      return Object.fromEntries(Object.keys(entry).sort().map((key) => [key, entry[key]]))
    }
    return entry
  })
}

/** Revive arrays em vez de objetos `{"0":...}` ao ler o banco. */
export function reviveArrays(_key, value) {
  if (value && typeof value === 'object' && !Array.isArray(value)) {
    const keys = Object.keys(value)
    if (keys.length > 0 && keys.every((key, index) => key === String(index))) {
      return keys.map((key) => value[key])
    }
  }
  return value
}

export function sendJson(res, status, body) {
  const payload = JSON.stringify(body ?? {})
  res.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'content-length': Buffer.byteLength(payload),
    'cache-control': 'no-store',
  })
  res.end(payload)
}

export function sendError(res, status, message) {
  sendJson(res, status, { error: message })
}

export async function readBody(req, limit = 64 * 1024) {
  const chunks = []
  let size = 0
  for await (const chunk of req) {
    size += chunk.length
    if (size > limit) {
      throw new Error('corpo da requisição grande demais')
    }
    chunks.push(chunk)
  }
  if (size === 0) return {}
  return JSON.parse(Buffer.concat(chunks).toString('utf8'))
}

/** Tamanho máximo de uma mensagem recebida pelo WebSocket do site. */
export const WS_MAX_MESSAGE = 16 * 1024

export function sha256(value) {
  return crypto.createHash('sha256').update(value).digest('hex')
}
