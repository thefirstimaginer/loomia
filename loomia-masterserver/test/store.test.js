// Testes do repositório de dados (contas e catálogo de mapas).

import assert from 'node:assert/strict'
import { test } from 'node:test'
import fs from 'node:fs/promises'
import os from 'node:os'
import path from 'node:path'
import { Store } from '../lib/store.js'

async function tempStore() {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), 'loomia-store-'))
  return { dir, store: await new Store(dir).load() }
}

test('registro e autenticação de contas', async () => {
  const { dir, store } = await tempStore()
  const user = await store.register('Ana', 'segredo123')
  assert.equal(user.name, 'Ana')
  assert.equal(store.authenticate('ana', 'segredo123').id, user.id)
  assert.equal(store.authenticate('Ana', 'errada'), null)
  assert.equal(store.authenticate('Ninguem', 'segredo123'), null)
  await assert.rejects(() => store.register('ANA', 'outrasenha'))
  await fs.rm(dir, { recursive: true, force: true })
})

test('usuário e senha sobrevivem a um reload do banco', async () => {
  const { dir, store } = await tempStore()
  await store.register('Caio', 'senha123')
  const reloaded = await new Store(dir).load()
  assert.ok(reloaded.authenticate('Caio', 'senha123'))
  await fs.rm(dir, { recursive: true, force: true })
})

test('catálogo de mapas lê o .loom e gera versão', async () => {
  const { dir, store } = await tempStore()
  const world = {
    id: 'teste',
    name: 'Mundo Teste',
    spawn: [0, 1, 0],
    sky_color: [150, 200, 240],
    blocks: [{ position: [0, 0, 0], size: [10, 1, 10], color: [100, 160, 60], material: 'grass' }],
  }
  const map = await store.saveWorld(world, 'Ana')
  assert.equal(map.id, 'teste')
  assert.equal(store.listMaps().length, 1)
  const loaded = await store.readWorld('teste')
  assert.equal(loaded.name, 'Mundo Teste')
  assert.match(store.worldVersion(loaded), /^teste-[0-9a-f]{12}$/)
  await assert.rejects(() => store.saveWorld({ id: 'sem-blocos' }, 'Ana'))
  await fs.rm(dir, { recursive: true, force: true })
})
