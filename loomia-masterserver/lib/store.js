// Banco de dados simples em JSON (sem dependências nativas).
//
//   users:  { id, name, nameLower, passHash, salt, appearance, createdAt }
//   maps:   { id, name, description, author, maxPlayers, visible, updatedAt }
//
// Os mapas em si (.loom) ficam em `data/worlds/<id>.loom`; o banco guarda só os
// metadados do catálogo, para o site listar sem ler os arquivos enormes.

import fs from 'node:fs/promises'
import path from 'node:path'
import crypto from 'node:crypto'
import { readJson, writeJson, sha256, stableJson } from './util.js'

export class Store {
  constructor(dataDir) {
    this.dir = dataDir
    this.dbFile = path.join(dataDir, 'masterserver.json')
    this.worldsDir = path.join(dataDir, 'worlds')
    this.data = { users: [], maps: [], nextUserId: 1 }
    this.writeChain = Promise.resolve()
  }

  async load() {
    await fs.mkdir(this.worldsDir, { recursive: true })
    const loaded = await readJson(this.dbFile, null)
    if (loaded) {
      this.data = { nextUserId: 1, users: [], maps: [], ...loaded }
    }
    if (!loaded) await this.save()
    return this
  }

  // Escritas serializadas: dois requests simultâneos não se sobrescrevem.
  save() {
    this.writeChain = this.writeChain.then(() => writeJson(this.dbFile, this.data))
    return this.writeChain
  }

  pathOf(mapId) {
    return path.join(this.worldsDir, `${mapId}.loom`)
  }

  // ------------------------------------------------------------------ contas

  hashPassword(password, salt) {
    // scrypt é nativo e resistente a força bruta; o salt vai no registro.
    return crypto.scryptSync(password, salt, 32).toString('hex')
  }

  async register(name, password) {
    const nameLower = name.toLowerCase()
    if (this.data.users.some((user) => user.nameLower === nameLower)) {
      throw new Error('esse nome já está em uso')
    }
    const salt = crypto.randomBytes(12).toString('hex')
    const user = {
      id: this.data.nextUserId++,
      name,
      nameLower,
      salt,
      passHash: this.hashPassword(password, salt),
      appearance: null,
      createdAt: new Date().toISOString(),
    }
    this.data.users.push(user)
    await this.save()
    return user
  }

  authenticate(name, password) {
    const user = this.data.users.find((entry) => entry.nameLower === String(name).toLowerCase())
    if (!user) return null
    const hash = this.hashPassword(password, user.salt)
    const same = crypto.timingSafeEqual(Buffer.from(hash, 'hex'), Buffer.from(user.passHash, 'hex'))
    return same ? user : null
  }

  async setAppearance(userId, appearance) {
    const user = this.data.users.find((entry) => entry.id === userId)
    if (!user) return null
    user.appearance = appearance
    await this.save()
    return user
  }

  // ------------------------------------------------------------------ mapas

  publicMapMeta(map) {
    return {
      id: map.id,
      name: map.name,
      description: map.description,
      author: map.author,
      max_players: map.maxPlayers,
          updated_at: map.updatedAt,
        }
      }

      listMaps({ includeHidden = false } = {}) {
        return this.data.maps
          .filter((map) => includeHidden || map.visible !== false)
          .map((map) => this.publicMapMeta(map))
      }

      findMap(id) {
        return this.data.maps.find((map) => map.id === id) ?? null
      }

      async readWorld(id) {
    const map = this.findMap(id)
    if (!map) return null
    return readJson(this.pathOf(id), null)
  }

  async saveWorld(world, authorName) {
    if (!world || typeof world.id !== 'string' || !Array.isArray(world.blocks)) {
      throw new Error('mapa inválido: precisa de "id" e "blocks"')
    }
    const id = world.id
    await writeJson(this.pathOf(id), world)
    let map = this.findMap(id)
    if (!map) {
      map = {
        id,
        name: world.name ?? id,
        description: '',
        author: authorName ?? 'comunidade',
        maxPlayers: 32,
        visible: true,
        updatedAt: new Date().toISOString(),
      }
      this.data.maps.push(map)
    }
    map.name = world.name ?? map.name
    map.updatedAt = new Date().toISOString()
    await this.save()
    return map
  }

  worldVersion(world) {
    return `${world.id}-${sha256(stableJson(world.blocks)).slice(0, 12)}`
  }
}
