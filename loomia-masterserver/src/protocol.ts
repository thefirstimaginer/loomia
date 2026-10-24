// Protocolo `loomia://` — elo entre o site e o cliente.
//
// O site gera `loomia://launch?server=host:port&token=...&map=...` e o sistema
// operacional entrega esse link ao cliente Loomia (AppImage), que abre a janela
// e inicia o jogo.
//
// REGRA: quem registra `loomia://` é o CLIENTE, uma única vez. O master só
// gera links e inspeciona o registro para diagnóstico.

import { spawnSync } from 'node:child_process'

export interface LaunchLink {
  action: string
  server: string
  token: string
  map: string
  name: string
  color: string
  origin: string
}

/** Interpreta o link customizado (`loomia://launch?...` e `loomia:launch?...`). */
export function parseLaunchUrl(url: string): LaunchLink | null {
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

/** Campos do link em `chave=valor` (o que o engine espera na linha de comando). */
export function launchArguments(link: string): string[] | null {
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

/** Script de exemplo oferecido no site (para quem não usa o AppImage). */
export function handlerScript(clientBin = 'loomia-client'): string {
  return `#!/bin/sh
# Loomia — abre o cliente a partir de um link loomia://
URL="$1"
if [ -z "$URL" ]; then
  echo "uso: loomia-launch loomia://launch?server=host:porta&token=..." >&2
  exit 2
fi
exec "${clientBin}" "$URL"
`
}

export interface ProtocolInspection {
  registered: boolean
  handler: string | null
}

/** Consulta o registro do protocolo (não altera nada). */
export function inspectProtocolHandler(): ProtocolInspection {
  if (process.platform === 'linux') {
    const result = spawnSync('xdg-mime', ['query', 'default', 'x-scheme-handler/loomia'], { encoding: 'utf-8' })
    const desktopFile = (result.stdout ?? '').trim()
    return { registered: Boolean(desktopFile), handler: desktopFile || null }
  }
  if (process.platform === 'win32') {
    const result = spawnSync('reg', ['query', 'HKCU\\Software\\Classes\\loomia\\shell\\open\\command'], { encoding: 'utf-8' })
    return { registered: result.status === 0, handler: (result.stdout ?? '').trim() || null }
  }
  return { registered: false, handler: null }
}
