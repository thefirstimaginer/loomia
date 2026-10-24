// Criptografia de senhas e tokens.
//
// * Senhas: scrypt (N=2^15, r=8, p=1) com salt de 16 bytes. O formato guardado é
//   `scrypt$N$r$p$<salt base64>$<hash base64>` — versionado, para trocar de
//   parâmetros no futuro sem quebrar as contas antigas.
// * Comparação com `timingSafeEqual` (tempo constante).
// * Tokens de sessão: 32 bytes aleatórios; o banco guarda só o SHA-256.

import { randomBytes, scrypt, timingSafeEqual, createHash } from 'node:crypto'
import { promisify } from 'node:util'

type ScryptOptions = { N: number; r: number; p: number; maxmem: number }
const scryptAsync = promisify(scrypt) as (
  password: string,
  salt: Buffer,
  keylen: number,
  options: ScryptOptions,
) => Promise<Buffer>

const PARAMS = { N: 2 ** 15, r: 8, p: 1, keylen: 64 }
/** Limite de memória do scrypt (precisa de 128*N*r bytes). */
const MAX_MEM = 128 * PARAMS.N * PARAMS.r * 2

export async function hashPassword(password: string): Promise<string> {
  const salt = randomBytes(16)
  const derived = await scryptAsync(password.normalize('NFKC'), salt, PARAMS.keylen, {
    N: PARAMS.N,
    r: PARAMS.r,
    p: PARAMS.p,
    maxmem: MAX_MEM,
  })
  return `scrypt$${PARAMS.N}$${PARAMS.r}$${PARAMS.p}$${salt.toString('base64')}$${derived.toString('base64')}`
}

export async function verifyPassword(password: string, stored: string): Promise<boolean> {
  const parts = stored.split('$')
  if (parts.length !== 6 || parts[0] !== 'scrypt') return false
  const [, n, r, p, saltB64, hashB64] = parts
  const salt = Buffer.from(saltB64, 'base64')
  const expected = Buffer.from(hashB64, 'base64')
  const derived = await scryptAsync(password.normalize('NFKC'), salt, expected.length, {
    N: Number(n),
    r: Number(r),
    p: Number(p),
    maxmem: 128 * Number(n) * Number(r) * 2,
  })
  if (derived.length !== expected.length) return false
  return timingSafeEqual(derived, expected)
}

/** Token opaco (vai no cookie). */
export function newSessionToken(): string {
  return randomBytes(32).toString('base64url')
}

/** Hash do token (é o que vai ao banco). */
export function hashToken(token: string): string {
  return createHash('sha256').update(token).digest('hex')
}

/** Dummy hash para igualar o tempo de resposta em "email inexistente". */
export const DUMMY_HASH =
  'scrypt$32768$8$1$' + Buffer.alloc(16).toString('base64') + '$' + Buffer.alloc(64).toString('base64')
