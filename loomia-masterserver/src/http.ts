// Camada HTTP mínima: roteador por método+regex, JSON, cookies e erros.
//
// Sem framework: o master só precisa de uma API pequena e previsível.

import type { IncomingMessage, ServerResponse } from 'node:http'

export class HttpError extends Error {
  constructor(
    readonly status: number,
    message: string,
    readonly code?: string,
  ) {
    super(message)
  }
}

export type Handler = (ctx: RequestContext) => Promise<void> | void

export interface RequestContext {
  req: IncomingMessage
  res: ServerResponse
  url: URL
  params: string[]
  query: URLSearchParams
  body: <T = unknown>(limit?: number) => Promise<T>
  cookies: Map<string, string>
  ip: string
}

interface Route {
  method: string
  pattern: RegExp
  handler: Handler
}

export class Router {
  private routes: Route[] = []

  add(method: string, pattern: RegExp, handler: Handler): void {
    this.routes.push({ method, pattern, handler })
  }

  get(pattern: RegExp, handler: Handler): void {
    this.add('GET', pattern, handler)
  }

  post(pattern: RegExp, handler: Handler): void {
    this.add('POST', pattern, handler)
  }

  put(pattern: RegExp, handler: Handler): void {
    this.add('PUT', pattern, handler)
  }

  delete(pattern: RegExp, handler: Handler): void {
    this.add('DELETE', pattern, handler)
  }

  match(method: string, pathname: string): { handler: Handler; params: string[] } | null {
    for (const route of this.routes) {
      if (route.method !== method) continue
      const match = route.pattern.exec(pathname)
      if (match) return { handler: route.handler, params: match.slice(1) }
    }
    return null
  }
}

export interface JsonResultInit {
  status?: number
  headers?: Record<string, string>
}

export function sendJson(res: ServerResponse, status: number, body: unknown, headers: Record<string, string> = {}): void {
  const payload = JSON.stringify(body ?? null)
  res.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'content-length': Buffer.byteLength(payload),
    'cache-control': 'no-store',
    ...headers,
  })
  res.end(payload)
}

export function sendText(res: ServerResponse, status: number, body: string, contentType = 'text/plain; charset=utf-8'): void {
  res.writeHead(status, { 'content-type': contentType, 'cache-control': 'no-store' })
  res.end(body)
}

export function readBody<T = unknown>(req: IncomingMessage, limit = 64 * 1024): Promise<T> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = []
    let size = 0
    req.on('data', (chunk: Buffer) => {
      size += chunk.length
      if (size > limit) {
        reject(new HttpError(413, 'corpo da requisição grande demais'))
        req.destroy()
        return
      }
      chunks.push(chunk)
    })
    req.on('end', () => {
      if (size === 0) return resolve({} as T)
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString('utf8')) as T)
      } catch {
        reject(new HttpError(400, 'JSON inválido'))
      }
    })
    req.on('error', reject)
  })
}

export function parseCookies(header: string | undefined): Map<string, string> {
  const cookies = new Map<string, string>()
  if (!header) return cookies
  for (const part of header.split(';')) {
    const index = part.indexOf('=')
    if (index < 0) continue
    const name = part.slice(0, index).trim()
    const value = part.slice(index + 1).trim()
    if (!name) continue
    try {
      cookies.set(name, decodeURIComponent(value))
    } catch {
      cookies.set(name, value)
    }
  }
  return cookies
}

export interface CookieOptions {
  maxAge?: number
  httpOnly?: boolean
  secure?: boolean
  sameSite?: 'Lax' | 'Strict' | 'None'
  path?: string
}

export function serializeCookie(name: string, value: string, options: CookieOptions = {}): string {
  const parts = [`${name}=${encodeURIComponent(value)}`]
  parts.push(`Path=${options.path ?? '/'}`)
  if (options.maxAge !== undefined) parts.push(`Max-Age=${Math.floor(options.maxAge)}`)
  if (options.httpOnly !== false) parts.push('HttpOnly')
  if (options.secure) parts.push('Secure')
  parts.push(`SameSite=${options.sameSite ?? 'Lax'}`)
  return parts.join('; ')
}

export function clientIp(req: IncomingMessage): string {
  const forwarded = req.headers['x-forwarded-for']
  if (typeof forwarded === 'string' && forwarded.length > 0) {
    return forwarded.split(',')[0]!.trim()
  }
  const remote = req.socket.remoteAddress ?? 'desconhecido'
  return remote.startsWith('::ffff:') ? remote.slice(7) : remote
}
