// Seed do banco: catálogo inicial a partir dos `.loom` em data/worlds.
//
//   npm run seed    (aplica as migrations e publica os mapas em disco)

import { readdir, readFile } from 'node:fs/promises'
import path from 'node:path'
import { config } from '../config.js'
import { migrate, one, query } from './index.js'
import { createLogger } from '../util.js'

const DEFAULT_MAPS: Record<string, { description: string; author: string }> = {
  baseplate: {
    description: 'Baseplate clássica de 2008, com torres para escalar.',
    author: 'Loomia',
  },
}

export async function seed(): Promise<number> {
  const log = createLogger()
  await migrate()

  let entries: string[] = []
  try {
    entries = (await readdir(config.worldsDir)).filter((file) => file.endsWith('.loom'))
  } catch {
    return 0
  }

  let published = 0
  for (const entry of entries) {
    const id = entry.slice(0, -'.loom'.length)
    if (await one('SELECT id FROM maps WHERE id = $1', [id])) continue

    let world: { name?: string; description?: string; blocks?: unknown[] }
    try {
      world = JSON.parse(await readFile(path.join(config.worldsDir, entry), 'utf8'))
    } catch {
      continue
    }
    if (!Array.isArray(world.blocks)) continue

    const meta = DEFAULT_MAPS[id] ?? { description: '', author: 'comunidade' }
    await query(
      `INSERT INTO maps (id, name, description, author_name, blocks, visible)
       VALUES ($1, $2, $3, $4, $5, true)
       ON CONFLICT (id) DO NOTHING`,
      [id, world.name ?? id, world.description ?? meta.description, meta.author, world.blocks.length],
    )
    published += 1
    log.info(`seed: mapa "${id}" publicado (${world.blocks.length} blocos)`)
  }
  return published
}

const invokedDirectly = process.argv[1]?.endsWith('seed.ts') || process.argv[1]?.endsWith('seed.js')
if (invokedDirectly) {
  seed()
    .then((count) => {
      console.log(`seed concluído (${count} mapa(s) novo(s))`)
      process.exit(0)
    })
    .catch((error) => {
      console.error('seed falhou:', error)
      process.exit(1)
    })
}
