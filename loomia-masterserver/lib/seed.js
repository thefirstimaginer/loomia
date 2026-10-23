// Seed do catálogo: no primeiro boot, publica os mapas `.loom` deste repositório
// (os que já existiam em `data/worlds`) para o site não abrir vazio.

import fs from 'node:fs/promises'
import { readJson } from './util.js'

const DEFAULT_MAPS = {
  baseplate: {
    description: 'Baseplate clássica de 2008, com torres para escalar.',
    author: 'Loomia',
  },
}

export async function seedWorlds(store, log) {
  if (store.listMaps({ includeHidden: true }).length > 0) return

  let entries = []
  try {
    entries = await fs.readdir(store.worldsDir)
  } catch {
    return
  }

  for (const entry of entries) {
    if (!entry.endsWith('.loom')) continue
    const id = entry.slice(0, -'.loom'.length)
    const world = await readJson(store.pathOf(id), null)
    if (!world || !Array.isArray(world.blocks)) continue
    const meta = DEFAULT_MAPS[id] ?? {}
    const map = await store.saveWorld({ ...world, id }, meta.author ?? 'comunidade')
    map.description = meta.description ?? `${world.blocks.length} blocos`
    log.info(`catálogo: mapa "${map.name}" (${map.id}) publicado a partir de ${entry}`)
  }
  await store.save()
}
