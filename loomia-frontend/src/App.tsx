import { useCallback, useEffect, useMemo, useState } from 'react'
import { api, type LaunchStatus, type ServerStatus, type WorldInfo, type WorldMap } from './api'
import { WorldPreview } from './WorldPreview'

const HATS = [
  { id: '', label: 'Sem chapéu' },
  { id: 'cap', label: 'Boné' },
  { id: 'top_hat', label: 'Cartola' },
  { id: 'propeller', label: 'Hélice' },
  { id: 'crown', label: 'Coroa' },
] as const

const BODY_COLORS = ['#eabe19', '#e0af8c', '#8c5a3c', '#f5d2aa', '#78c88c', '#bebec8']
const SHIRT_COLORS = ['#0f78be', '#be2828', '#e6e6e6', '#464646', '#f0a014', '#6e3ca0']
const PANTS_COLORS = ['#234b2d', '#1e1e28', '#5a4632', '#3c3c78', '#787878']

function hexToRgb(hex: string): [number, number, number] {
  const value = hex.replace('#', '')
  const n = parseInt(value.length === 3 ? value.split('').map((c) => c + c).join('') : value, 16)
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255]
}

function statusLabel(state: 'checking' | 'online' | 'offline') {
  if (state === 'checking') return 'verificando…'
  if (state === 'online') return 'servidor online'
  return 'servidor offline'
}

export default function App() {
  const [status, setStatus] = useState<ServerStatus | null>(null)
  const [connection, setConnection] = useState<'checking' | 'online' | 'offline'>('checking')
  const [worlds, setWorlds] = useState<WorldInfo[]>([])
  const [map, setMap] = useState<WorldMap | null>(null)
  const [playerName, setPlayerName] = useState(() => localStorage.getItem('loomia:name') ?? 'Loomian')
  const [hat, setHat] = useState<string>('')
  const [body, setBody] = useState(BODY_COLORS[0])
  const [shirt, setShirt] = useState(SHIRT_COLORS[0])
  const [pants, setPants] = useState(PANTS_COLORS[0])
  const [launchState, setLaunchState] = useState<'idle' | 'launching' | 'launched' | 'error'>('idle')
  const [launchMessage, setLaunchMessage] = useState('')
  const [launchStatus, setLaunchStatus] = useState<LaunchStatus | null>(null)

  const refreshStatus = useCallback(async () => {
    try {
      const data = await api<ServerStatus>('/api/status')
      setStatus(data)
      setConnection('online')
    } catch {
      setConnection('offline')
    }
  }, [])

  useEffect(() => {
    void refreshStatus()
    const timer = setInterval(() => void refreshStatus(), 4000)
    return () => clearInterval(timer)
  }, [refreshStatus])

  useEffect(() => {
    api<WorldInfo[]>('/api/worlds')
      .then(async (list) => {
        setWorlds(list)
        if (list[0]) {
          const fullMap = await api<WorldMap>(`/api/worlds/${list[0].id}`)
          setMap(fullMap)
        }
      })
      .catch(() => {
        // O servidor pode não estar no ar ainda: a prévia mostra "carregando".
      })
  }, [])

  // Polling do progresso de compilação enquanto o cliente é preparado.
  useEffect(() => {
    if (launchState !== 'launching') return
    let cancelled = false
    const poll = async () => {
      try {
        const progress = await api<LaunchStatus>('/api/launch/status')
        if (!cancelled) setLaunchStatus(progress)
      } catch {
        /* ignora */
      }
    }
    void poll()
    const timer = setInterval(() => void poll(), 1500)
    return () => {
      cancelled = true
      clearInterval(timer)
    }
  }, [launchState])

  const launch = async () => {
    const name = playerName.trim().slice(0, 20) || 'Loomian'
    setPlayerName(name)
    localStorage.setItem('loomia:name', name)
    setLaunchState('launching')
    setLaunchMessage('Preparando o cliente Loomia…')

    try {
      // Guarda a aparência no servidor (registro de jogadores persistido).
      await api('/api/player', {
        method: 'POST',
        body: JSON.stringify({
          username: name,
          body_color: hexToRgb(body),
          shirt_color: hexToRgb(shirt),
          pants_color: hexToRgb(pants),
          hat: hat || null,
        }),
      }).catch(() => undefined)

      const result = await api<{ ok?: boolean; pid?: number; message?: string; error?: string }>('/api/launch', {
        method: 'POST',
        body: JSON.stringify({ playerName: name, world: worlds[0]?.id }),
      })

      setLaunchState('launched')
      setLaunchMessage(result.message ?? 'Cliente iniciado!')
    } catch (error) {
      setLaunchState('error')
      setLaunchMessage(
        error instanceof Error
          ? `${error.message} — se o servidor estiver rodando, inicie o cliente manualmente com "cargo run -p loomia-client".`
          : 'Falha ao iniciar o cliente.',
      )
    }
  }

  const players = status?.players ?? []
  const humanPlayers = players.filter((p) => !p.name.startsWith('Loomian'))
  const displayedPlayers = humanPlayers.length > 0 ? humanPlayers : players

  const avatarColors = useMemo(
    () => ({ body: hexToRgb(body), shirt: hexToRgb(shirt), pants: hexToRgb(pants) }),
    [body, shirt, pants],
  )

  return (
    <div className="loomia">
      <header className="topbar">
        <div className="brand">
          <span className="brand-logo">LOOMIA</span>
          <span className="brand-tag">build · play · share</span>
        </div>
        <nav className="topnav">
          <a href="#play">Jogar</a>
          <a href="#world">Mundo</a>
          <a href="#players">Jogadores</a>
          <a href="#about">Sobre</a>
        </nav>
        <div className={`status-pill ${connection}`}>
          <span className="dot" />
          {statusLabel(connection)}
          {status && (
            <span className="status-meta">
              v{status.version} · {status.tick_rate} Hz
            </span>
          )}
        </div>
      </header>

      <main>
        <section className="hero" id="play">
          <div className="hero-copy">
            <p className="eyebrow">cliente nativo em rust + bevy · servidor autoritativo em rust</p>
            <h1>
              Entre no mundo.
              <br />
              Construa como em 2008.
            </h1>
            <p className="lead">
              Loomia é um jogo online de mundos em blocos. O site é o lobby; o jogo é um cliente 3D em Rust que
              abre em uma janela própria — com física, chat e multiplayer em tempo real.
            </p>

            <div className="launch-card">
              <label className="field">
                <span>Seu nome de jogador</span>
                <input
                  value={playerName}
                  maxLength={20}
                  onChange={(event) => setPlayerName(event.target.value)}
                  placeholder="Loomian"
                />
              </label>

              <div className="avatar-editor">
                <div className="avatar-preview" aria-hidden>
                  <div className="av-head" style={{ background: body }}>
                    {hat === 'cap' && <div className="av-cap" style={{ background: '#c82d2d' }} />}
                    {hat === 'top_hat' && (
                      <>
                        <div className="av-tophat-brim" />
                        <div className="av-tophat" />
                      </>
                    )}
                    {hat === 'propeller' && (
                      <>
                        <div className="av-cap" style={{ background: '#fac828' }} />
                        <div className="av-propeller" />
                      </>
                    )}
                    {hat === 'crown' && <div className="av-crown" />}
                    <div className="av-eyes">
                      <span />
                      <span />
                    </div>
                  </div>
                  <div className="av-torso" style={{ background: shirt }} />
                  <div className="av-legs" style={{ background: pants }} />
                </div>

                <div className="avatar-controls">
                  <ColorRow label="Pele" colors={BODY_COLORS} value={body} onChange={setBody} />
                  <ColorRow label="Camisa" colors={SHIRT_COLORS} value={shirt} onChange={setShirt} />
                  <ColorRow label="Calça" colors={PANTS_COLORS} value={pants} onChange={setPants} />
                  <label className="field compact">
                    <span>Chapéu</span>
                    <select value={hat} onChange={(event) => setHat(event.target.value)}>
                      {HATS.map((option) => (
                        <option key={option.id} value={option.id}>
                          {option.label}
                        </option>
                      ))}
                    </select>
                  </label>
                </div>
              </div>

              <button
                className="launch-button"
                onClick={() => void launch()}
                disabled={!status || launchState === 'launching'}
              >
                <span className="play-icon">▶</span>
                {launchState === 'launching' ? 'LANÇANDO…' : 'LANÇAR LOOMIA'}
              </button>

              <div className={`launch-feedback ${launchState}`}>
                {launchState === 'idle' && 'O cliente abre em uma nova janela (Carregando Loomia, Aguarde!).'}
                {launchState === 'launching' && (
                  <>
                    <span>{launchMessage}</span>
                    {launchStatus?.building ? (
                      <code className="compile-line">{launchStatus.last_line}</code>
                    ) : (
                      !launchStatus?.built && (
                        <code className="compile-line">
                          compilando o cliente pela primeira vez (pode levar alguns minutos)…
                        </code>
                      )
                    )}
                  </>
                )}
                {launchState === 'launched' && `✔ ${launchMessage}`}
                {launchState === 'error' && `✖ ${launchMessage}`}
                {status && (
                  <small>
                    WebSocket: <code>{status.ws_url}</code> · cliente{' '}
                    {status.client.built ? 'compilado' : 'ainda não compilado'}
                  </small>
                )}
              </div>
            </div>
          </div>

          <div className="hero-visual">
            <WorldPreview map={map} />
            <div className="hero-visual-caption">
              <strong>{map?.name ?? 'Baseplate Clássica'}</strong>
              <span>{worlds[0]?.description ?? 'Prévia do mundo servido pelo servidor.'}</span>
            </div>
          </div>
        </section>

        <section className="cards" id="world">
          <article className="card">
            <h2>Mundo</h2>
            <p>{status ? `${status.map.blocks} blocos em "${status.map.name}"` : 'Aguardando o servidor…'}</p>
            <ul>
              <li>
                Mapa de teste gerado no servidor e salvo em <code>data/worlds/baseplate.loom</code>
              </li>
              <li>Blocos com cor e material (grama, tijolo, madeira, pedra, neon)</li>
              <li>Colisão autoritativa: o servidor valida cada passo</li>
            </ul>
          </article>

          <article className="card" id="players">
            <h2>Jogadores</h2>
            <p>{status ? `${status.players_online} online agora` : '…'}</p>
            <ul className="player-list">
              {displayedPlayers.length === 0 && <li className="muted">Ninguém no mundo ainda — lance o jogo!</li>}
              {displayedPlayers.map((player) => (
                <li key={player.id}>
                  <span className="player-dot" />
                  {player.name}
                  <small>
                    {player.position[0].toFixed(1)}, {player.position[1].toFixed(1)}, {player.position[2].toFixed(1)}
                  </small>
                </li>
              ))}
            </ul>
          </article>

          <article className="card" id="about">
            <h2>Arquitetura</h2>
            <ul>
              <li>
                <strong>loomia-shared</strong> — protocolo, física e mundo (cliente e servidor usam o mesmo crate)
              </li>
              <li>
                <strong>loomia-server</strong> — Axum + Tokio, 30 Hz autoritativos, WebSocket em <code>/ws</code>
              </li>
              <li>
                <strong>loomia-client</strong> — Bevy 3D, previsão local + reconciliação
              </li>
            </ul>
            <p className="muted">
              Latência: {status ? `${status.sessions} sessões` : '—'} · chat: {status ? status.total_chat_messages : 0}{' '}
              msgs
            </p>
          </article>
        </section>
      </main>

      <footer>
        <span>Loomia {status ? `v${status.version}` : ''} — inspirado no Roblox clássico de 2008</span>
        <span className="avatar-colors" style={{ display: 'none' }}>
          {avatarColors.body.join(',')}
        </span>
      </footer>
    </div>
  )
}

function ColorRow({
  label,
  colors,
  value,
  onChange,
}: {
  label: string
  colors: string[]
  value: string
  onChange: (color: string) => void
}) {
  return (
    <div className="color-row">
      <span>{label}</span>
      <div className="swatches">
        {colors.map((color) => (
          <button
            key={color}
            type="button"
            className={`swatch ${value === color ? 'selected' : ''}`}
            style={{ background: color }}
            aria-label={`${label} ${color}`}
            onClick={() => onChange(color)}
          />
        ))}
      </div>
    </div>
  )
}
