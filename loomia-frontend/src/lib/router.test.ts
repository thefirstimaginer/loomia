import { describe, it, expect } from 'vitest'
import { parseRoute, worldPath } from './router'

describe('parseRoute', () => {
  it('landing', () => {
    expect(parseRoute('/')).toEqual({ name: 'landing' })
  })
  it('explore', () => {
    expect(parseRoute('/explore')).toEqual({ name: 'explore' })
    expect(parseRoute('/mundos')).toEqual({ name: 'explore' })
  })
  it('world por ID', () => {
    expect(parseRoute('/world/baseplate')).toEqual({ name: 'world', mapId: 'baseplate' })
    expect(parseRoute('/jogar/minhamapa')).toEqual({ name: 'world', mapId: 'minhamapa' })
  })
  it('world com slug', () => {
    expect(parseRoute('/world/baseplate/minha-base')).toEqual({
      name: 'world',
      mapId: 'baseplate',
    })
  })
  it('login e registrar', () => {
    expect(parseRoute('/login')).toEqual({ name: 'login' })
    expect(parseRoute('/entrar')).toEqual({ name: 'login' })
    expect(parseRoute('/registrar')).toEqual({ name: 'register' })
    expect(parseRoute('/criar-conta')).toEqual({ name: 'register' })
  })
  it('boas-vindas', () => {
    expect(parseRoute('/boas-vindas')).toEqual({ name: 'welcome' })
    expect(parseRoute('/onboarding')).toEqual({ name: 'welcome' })
  })
  it('perfil', () => {
    expect(parseRoute('/perfil')).toEqual({ name: 'profile' })
  })
  it('club / corner', () => {
    expect(parseRoute('/club')).toEqual({ name: 'club' })
    expect(parseRoute('/corner')).toEqual({ name: 'club' })
    expect(parseRoute('/loomiers-corner')).toEqual({ name: 'club' })
  })
  it('forum', () => {
    expect(parseRoute('/forum')).toEqual({ name: 'forum' })
    expect(parseRoute('/forum/geral')).toEqual({ name: 'forumRoom', roomId: 'geral' })
  })
  it('blog', () => {
    expect(parseRoute('/blog')).toEqual({ name: 'blog' })
  })
  it('download', () => {
    expect(parseRoute('/download')).toEqual({ name: 'download' })
  })
  it('player', () => {
    expect(parseRoute('/jogadores/42')).toEqual({ name: 'player', playerId: '42' })
  })
  it('amigos e tickets', () => {
    expect(parseRoute('/amigos')).toEqual({ name: 'friends' })
    expect(parseRoute('/tickets')).toEqual({ name: 'tickets' })
    expect(parseRoute('/suporte')).toEqual({ name: 'tickets' })
  })
  it('404 para rota desconhecida', () => {
    expect(parseRoute('/qualquer-coisa')).toEqual({ name: 'notFound' })
  })
})

describe('worldPath', () => {
  it('gera slug amigável', () => {
    expect(worldPath('baseplate', 'Minha Base!')).toBe('/world/baseplate/minha-base')
  })
  it('sem nome usa só o ID', () => {
    expect(worldPath('baseplate')).toBe('/world/baseplate')
  })
  it('remove acentos e caracteres especiais', () => {
    expect(worldPath('mapa1', 'Café & Leite')).toBe('/world/mapa1/cafe-leite')
  })
  it('limita slug a 40 caracteres', () => {
    const long = 'a'.repeat(60)
    expect(worldPath('m', long).length).toBeLessThanOrEqual('/world/m/'.length + 40)
  })
})
