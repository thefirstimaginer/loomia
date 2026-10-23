// Protocolo `loomia://` — o elo entre o site e o cliente.
//
// O site gera `loomia://launch?server=host:port&token=...&map=...` e o sistema
// operacional entrega esse link ao **cliente Loomia** (AppImage), que abre a
// janela e inicia o jogo.
//
// REGRA IMPORTANTE: quem registra `loomia://` é o CLIENTE, uma única vez
// (no primeiro clique). O master só *gera e inspeciona* links — ele roda na
// nuvem, onde não faz sentido mexer no desktop de ninguém.

import fs from 'node:fs/promises'
import fsSync from 'node:fs'
import path from 'node:path'
import { spawnSync } from 'node:child_process'

/** Interpreta o link customizado (aceita `loomia://launch` e `loomia:launch`). */
export function parseLaunchUrl(url) {
  const trimmed = String(url ?? '').trim().replace(/^["']|["']$/g, '')
  const match = trimmed.match(/^loomia:\/*([^?#]*)(?:\?(.*))?$/i)
  if (!match) return null
  const query = new URLSearchParams(match[2] ?? '')
  return {
    action: match[1] || 'launch',
    server: query.get('server') ?? '',
    token: query.get('token') ?? '',
    map: query.get('map') ?? '',
    name: query.get('name') ?? 'Loomian',
    color: query.get('color') ?? '',
    origin: query.get('origin') ?? '',
  }
}

/** Vira os campos do link para a linha de comando do engine. */
export function launchArguments(link) {
  const parsed = parseLaunchUrl(link)
  if (!parsed) return null
  return [
    `server=${parsed.server}`,
    `token=${parsed.token}`,
    `map=${parsed.map}`,
    `name=${parsed.name}`,
    `color=${parsed.color}`,
    `origin=${parsed.origin}`,
  ]
}

/** Script de exemplo para o site oferecer (quem não usa o AppImage). */
export function handlerScript({ clientBin = 'loomia-client' } = {}) {
  return `#!/bin/sh
# Loomia — abre o cliente a partir de um link loomia://
#
# Instalação manual (quem não usa o AppImage):
#   cp loomia-launch.sh ~/.local/bin/loomia-launch
#   xdg-mime default loomia-cliente.desktop x-scheme-handler/loomia
URL="$1"
if [ -z "$URL" ]; then
  echo "uso: loomia-launch loomia://launch?server=host:porta&token=..." >&2
  exit 2
fi
exec "${clientBin}" "$URL"
`
}

/**
 * Inspeção do registro do protocolo na máquina (diagnóstico do site).
 * Nunca registra nada: devolve o handler atual e se ele parece válido.
 */
export function inspectProtocolHandler() {
  if (process.platform === 'linux') {
    const result = spawnSync('xdg-mime', ['query', 'default', 'x-scheme-handler/loomia'], {
      encoding: 'utf-8',
    })
    const desktopFile = (result.stdout ?? '').trim()
    return { registered: Boolean(desktopFile), handler: desktopFile || null }
  }
  if (process.platform === 'win32') {
    const result = spawnSync(
      'reg',
      ['query', 'HKCU\\Software\\Classes\\loomia\\shell\\open\\command'],
      { encoding: 'utf-8' },
    )
    return { registered: result.status === 0, handler: (result.stdout ?? '').trim() || null }
  }
  return { registered: false, handler: null }
}

/** Cópia do script para o visitante baixar (instalação manual do cliente). */
export function writePortableHandler(config) {
  const dir = path.join(config.root, 'public', 'download')
  fsSync.mkdirSync(dir, { recursive: true })
  const file = path.join(dir, 'loomia-launch.sh')
  fsSync.writeFileSync(file, handlerScript(config), { mode: 0o755 })
  return file
}

export async function ensureLauncherDir(config) {
  const dir = path.join(config.dataDir, 'launcher')
  await fs.mkdir(dir, { recursive: true })
  return dir
}
