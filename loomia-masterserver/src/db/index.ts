// Acesso ao PostgreSQL: pool + migrations + helpers de consulta.
//
// As tabelas são criadas por migrations versionadas (`src/db/migrations`): na
// inicialização o servidor aplica só as que ainda não rodaram.

import { existsSync } from 'node:fs'
import { readdir, readFile } from 'node:fs/promises'
import path from 'node:path'
import { fileURLToPath } from 'node:url'
import pg from 'pg'
import { config } from '../config.js'

const { Pool } = pg

export const pool = new Pool({
  connectionString: config.databaseUrl,
  ssl: config.databaseSsl ? { rejectUnauthorized: false } : undefined,
  max: Number(process.env.LOOMIA_DB_POOL ?? 10),
  idleTimeoutMillis: 30_000,
})

pool.on('error', (error) => {
  console.error('[db] erro em conexão ociosa:', error.message)
})

/** Uma consulta avulsa. */
export async function query<T extends pg.QueryResultRow = pg.QueryResultRow>(
  text: string,
  params: unknown[] = [],
): Promise<pg.QueryResult<T>> {
  return pool.query<T>(text, params as never[])
}

/** Uma linha ou null. */
export async function one<T extends pg.QueryResultRow = pg.QueryResultRow>(
  text: string,
  params: unknown[] = [],
): Promise<T | null> {
  const result = await query<T>(text, params)
  return result.rows[0] ?? null
}

/** Transação: o callback recebe um cliente exclusivo. */
export async function transaction<T>(fn: (client: pg.PoolClient) => Promise<T>): Promise<T> {
  const client = await pool.connect()
  try {
    await client.query('BEGIN')
    const result = await fn(client)
    await client.query('COMMIT')
    return result
  } catch (error) {
    await client.query('ROLLBACK').catch(() => { })
    throw error
  } finally {
    client.release()
  }
}

function here(): string {
  return path.dirname(fileURLToPath(import.meta.url))
}

/**
 * Pasta das migrations. No build os `.sql` não são copiados para `dist/`, então
 * procuramos primeiro ao lado do código compilado e depois na fonte.
 */
export function migrationsDir(): string {
  const compiled = path.join(here(), 'migrations')
  if (existsSync(compiled)) return compiled
  // dist/src/db -> <projeto>/src/db
  const source = path.resolve(here(), '..', '..', '..', 'src', 'db', 'migrations')
  return source
}

/**
 * Aplica as migrations pendentes. A tabela `schema_migrations` guarda o que já
 * rodou; cada arquivo roda dentro de uma transação.
 */
export async function migrate(): Promise<string[]> {
  await query(`
    CREATE TABLE IF NOT EXISTS schema_migrations (
      name TEXT PRIMARY KEY,
      applied_at TIMESTAMPTZ NOT NULL DEFAULT now()
    )
  `)

  const dir = migrationsDir()
  const files = (await readdir(dir)).filter((file) => file.endsWith('.sql')).sort()
  const applied = new Set((await query<{ name: string }>('SELECT name FROM schema_migrations')).rows.map((r) => r.name))

  const ran: string[] = []
  for (const file of files) {
    if (applied.has(file)) continue
    const sql = await readFile(path.join(dir, file), 'utf8')
    await transaction(async (client) => {
      await client.query(sql)
      await client.query('INSERT INTO schema_migrations (name) VALUES ($1)', [file])
    })
    ran.push(file)
  }
  return ran
}

export async function closeDatabase(): Promise<void> {
  await pool.end()
}
