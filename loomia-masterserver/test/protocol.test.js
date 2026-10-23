// Testes do master server (node --test, sem dependências).

import assert from 'node:assert/strict'
import { test } from 'node:test'
import { parseLaunchUrl } from '../lib/protocol.js'
import { launchUrl } from '../lib/rooms.js'

test('parseLaunchUrl extrai os campos do link loomia://', () => {
  const parsed = parseLaunchUrl(
    'loomia://launch?server=127.0.0.1:7801&token=abc&map=baseplate&name=Ana%20Lima&color=eabe19',
  )
  assert.equal(parsed.action, 'launch')
  assert.equal(parsed.server, '127.0.0.1:7801')
  assert.equal(parsed.token, 'abc')
  assert.equal(parsed.map, 'baseplate')
  assert.equal(parsed.name, 'Ana Lima')
  assert.equal(parsed.color, 'eabe19')
})

test('parseLaunchUrl aceita a forma sem //', () => {
  const parsed = parseLaunchUrl('loomia:launch?server=10.0.0.2:7802&token=t')
  assert.equal(parsed.server, '10.0.0.2:7802')
  assert.equal(parsed.token, 't')
})

test('parseLaunchUrl rejeita links de outros esquemas', () => {
  assert.equal(parseLaunchUrl('https://loomia.com'), null)
  assert.equal(parseLaunchUrl(''), null)
})

test('launchUrl devolve um link que parseLaunchUrl entende', () => {
  const link = launchUrl(
    { host: '127.0.0.1', port: 7805, launchToken: 'tok', mapId: 'baseplate' },
    { playerName: 'Bob', color: 'ff0000', origin: 'http://127.0.0.1:8080/' },
  )
  assert.ok(link.startsWith('loomia://launch?'))
  const parsed = parseLaunchUrl(link)
  assert.equal(parsed.server, '127.0.0.1:7805')
  assert.equal(parsed.token, 'tok')
  assert.equal(parsed.map, 'baseplate')
  assert.equal(parsed.name, 'Bob')
  assert.equal(parsed.color, 'ff0000')
  assert.equal(parsed.origin, 'http://127.0.0.1:8080/')
})
