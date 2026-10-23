// Handler `loomia://` para a máquina do jogador.
//
// O site gera um link `loomia://launch?server=host:port&token=...&map=...`; o
// sistema operacional o entrega a este script, que traduz para argumentos do
// LOVR:
//
//   lovr <clientDir> server=host:port token=... map=... name=... color=...
//
// Gerado automaticamente pelo master server em `data/launcher/`.

import fs from 'node:fs/promises'
import fsSync from 'node:fs'
import path from 'node:path'
import os from 'node:os'
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

/**
 * Script POSIX intermediário (shell puro, sem dependências): recebe a URL,
 * extrai os campos e abre o LOVR com o projeto do cliente.
 */
export function handlerScript({ lovrBin, clientDir }) {
  const extra = (process.env.LOOMIA_LOVR_ARGS ?? '')
    .split(/\s+/)
    .filter(Boolean)
    .map((arg) => ` ${arg}`)
    .join('')
  return `#!/bin/sh
# Gerado pelo master server do Loomia — converte loomia:// em argumentos do LOVR.
URL="$1"
field() {
  printf '%s' "$URL" | sed -n "s/.*[?&]$1=\\([^&]*\\).*/\\1/p"
}
SERVER=$(field server)
TOKEN=$(field token)
MAP=$(field map)
NAME=$(field name)
COLOR=$(field color)
ORIGIN=$(field origin)

if [ -z "$SERVER" ] || [ -z "$TOKEN" ]; then
  echo "loomia: link inválido: $URL" >&2
  exit 2
fi

exec "${lovrBin}"${extra} "${clientDir}" \\
  server="$SERVER" token="$TOKEN" map="$MAP" name="$NAME" color="$COLOR" origin="$ORIGIN"
`
}

async function registerLinux(handlerPath) {
  const apps = path.join(os.homedir(), '.local', 'share', 'applications')
  await fs.mkdir(apps, { recursive: true })
  const desktopFile = path.join(apps, 'loomia-launcher.desktop')
  await fs.writeFile(
    desktopFile,
    `[Desktop Entry]
Type=Application
Name=Loomia
Comment=Abrir o cliente Loomia (LOVR)
Exec=${handlerPath} %u
Terminal=false
Categories=Game;
MimeType=x-scheme-handler/loomia;
`,
  )
  spawnSync('xdg-mime', ['default', 'loomia-launcher.desktop', 'x-scheme-handler/loomia'])
  spawnSync('update-desktop-database', [apps])
  return desktopFile
}

function registerWindows(handlerPath) {
  const command = `"${process.execPath}" "${handlerPath}" "%1"`
  spawnSync('reg', ['add', 'HKCU\\Software\\Classes\\loomia', '/ve', '/d', 'URL:Loomia Protocol', '/f'])
  spawnSync('reg', ['add', 'HKCU\\Software\\Classes\\loomia', '/v', 'URL Protocol', '/d', '', '/f'])
  spawnSync('reg', ['add', 'HKCU\\Software\\Classes\\loomia\\shell\\open\\command', '/ve', '/d', command, '/f'])
  return 'HKCU\\Software\\Classes\\loomia'
}

/**
 * Garante o script no disco e, quando `mode = 'auto'`, registra no SO (o
 * master roda na máquina do jogador em dev; em produção use 'none').
 */
export async function installProtocolHandler({ config, log }) {
  const dir = path.join(config.dataDir, 'launcher')
  await fs.mkdir(dir, { recursive: true })
  const handlerPath = path.join(dir, process.platform === 'win32' ? 'loomia-launch.cmd' : 'loomia-launch.sh')
  await fs.writeFile(handlerPath, handlerScript(config), { mode: 0o755 })

  if (config.protocolHandler === 'none') {
    log.info(`handler loomia:// gerado em ${handlerPath} (registro no SO desativado)`)
    return { handlerPath, registered: false }
  }

  try {
    if (process.platform === 'linux') {
      const desktopFile = await registerLinux(handlerPath)
      log.info(`loomia:// registrado no SO (${desktopFile})`)
      return { handlerPath, registered: true }
    }
    if (process.platform === 'win32') {
      const key = registerWindows(handlerPath)
      log.info(`loomia:// registrado no Windows (${key})`)
      return { handlerPath, registered: true }
    }
    log.warn('macOS: para registrar loomia:// crie um .app com CFBundleURLTypes (ver README)')
  } catch (error) {
    log.warn(`não foi possível registrar loomia://: ${error.message}`)
  }
  return { handlerPath, registered: false }
}

/** Cópia do script para o visitante baixar (instalação manual do cliente). */
export function writePortableHandler(config) {
  const dir = path.join(config.root, 'public', 'download')
  fsSync.mkdirSync(dir, { recursive: true })
  const file = path.join(dir, 'loomia-launch.sh')
  fsSync.writeFileSync(file, handlerScript(config), { mode: 0o755 })
  return file
}
