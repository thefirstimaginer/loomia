// Utilidades comuns: relógio, tokens, JSON em disco e logger.

import { randomBytes } from 'node:crypto'

export const now = (): number => Date.now()

export function randomToken(bytes = 18): string {
  return randomBytes(bytes).toString('base64url')
}

export interface Logger {
  debug: (...args: unknown[]) => void
  info: (...args: unknown[]) => void
  warn: (...args: unknown[]) => void
  error: (...args: unknown[]) => void
}

const LEVELS: Record<string, number> = { debug: 10, info: 20, warn: 30, error: 40 }

export function createLogger(level = process.env.LOOMIA_LOG ?? 'info'): Logger {
  const threshold = LEVELS[level] ?? 20
  const stamp = (): string => new Date().toISOString().slice(11, 19)
  return {
    debug: (...args) => threshold <= 10 && console.debug(`[${stamp()}] [debug]`, ...args),
    info: (...args) => threshold <= 20 && console.log(`[${stamp()}] [info]`, ...args),
    warn: (...args) => threshold <= 30 && console.warn(`[${stamp()}] [warn]`, ...args),
    error: (...args) => threshold <= 40 && console.error(`[${stamp()}] [error]`, ...args),
  }
}

/** Rate limit em memória por chave (IP/rota). */
export class RateLimiter {
  private buckets = new Map<string, { count: number; reset: number }>()

  constructor(private readonly sweepMs = 60_000) {
    const timer = setInterval(() => this.sweep(), sweepMs)
    timer.unref()
  }

  /** Consome uma tentativa; devolve os segundos de espera (0 = liberado). */
  take(key: string, max: number, windowMs: number): number {
    const time = now()
    const bucket = this.buckets.get(key)
    if (!bucket || bucket.reset < time) {
      this.buckets.set(key, { count: 1, reset: time + windowMs })
      return 0
    }
    bucket.count += 1
    if (bucket.count <= max) return 0
    return Math.ceil((bucket.reset - time) / 1000)
  }

  private sweep(): void {
    const time = now()
    for (const [key, bucket] of this.buckets) {
      if (bucket.reset < time) this.buckets.delete(key)
    }
  }
}
