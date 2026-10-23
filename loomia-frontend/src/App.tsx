import { useCallback, useEffect, useMemo, useState } from 'react'
import {
  api,
  type LaunchResponse,
  type MapInfo,
  type Status,
  type WorldMap,
} from './api'
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
  if (state === 'online') return 'lobby online'
  return 'lobby offline'
}

export default function App() {
  const [status, setStatus] = useState<Status | null>(null)
  const [connection, setConnection] = useState<'checking' | 'online' | 'offline'>('checking')
  const [maps, setMaps] = useState<MapInfo[]>([])
  const [selectedMap, setSelectedMap] = useState<string>('')
  const [map, setMap] = useState<WorldMap | null>(null)
  const [playerName, setPlayerName] = useState(() => localStorage.getItem('loomia:name') ?? 'Loomian')
  const [hat, setHat] = useState<string>('')
  const [body, setBody] = useState(BODY_COLORS[0])
  const [shirt, setShirt] = useState(SHIRT_COLORS[0])
  const [pants, setPants] = useState(PANTS_COLORS[0])
  const [launchState, setLaunchState] = useState<'idle' | 'launching' | 'launched' | 'error'>('idle')
  const [launchMessage, setLaunchMessage] = useState('')

  const refreshStatus = useCallback(async () => {
    try {
      const data = await api<Status>('/api/status')
      setStatus(data)
      setConnection('online')
    } catch {
      setConnection('offline')
    }
  }, [])

  const refreshMaps = useCallback(async () => {
    try {
      setMaps(await api<MapInfo[]>('/api/maps'))
    } catch {
      // O master pode não estar no ar ainda.
    }
  }, [])

  useEffect(() => {
    void refreshStatus()
    void refreshMaps()
    const timer = setInterval(() => {
      void refreshStatus()
      void refreshMaps()
    }, 4000)
    return () => clearInterval(timer)
  }, [refreshStatus, refreshMaps])

  // Mapa selecionado: alimenta a prévia 3D e o botão Jogar.
  const activeMapId = selectedMap || maps[0]?.id || ''
  useEffect(() => {
    if (!activeMapId) return
    let cancelled = false
    api<WorldMap>(`/api/maps/${activeMapId}`)
      .then((world) => {
        if (!cancelled) setMap(world)
      })
      .catch(() => {
        if (!cancelled) setMap(null)
      })
    return () => {
      cancelled = true
    }
  }, [activeMapId])

  /**
   * Botão "Jogar": o master aloca a sala (processo Go, UDP) e devolve o link
   * `loomia://launch?...`. O navegador abre esse link, o sistema operacional
   * entrega ao handler, e o cliente LOVR inicia já conectado à sala.
   */
  const launch = async () => {
    const name = playerName.trim().slice(0, 20) || 'Loomian'
    setPlayerName(name)
    localStorage.setItem('loomia:name', name)

    if (!activeMapId) {
      setLaunchState('error')
      setLaunchMessage('nenhum mapa disponível no catálogo ainda')
      return
    }

    setLaunchState('launching')
    setLaunchMessage('Alocando uma sala para o mapa…')

    try {
      const result = await api<LaunchResponse>('/api/launch', {
        method: 'POST',
        body: JSON.stringify({ map_id: activeMapId, name, color: body.replace('#', '') }),
      })
      setLaunchState('launched')
      setLaunchMessage(`${result.message}. Abrindo o cliente Loomia…`)
      window.location.href = result.launch_url
    } catch (error) {
      setLaunchState('error')
      setLaunchMessage(
        error instanceof Error
          ? `${error.message} — o master server está rodando em loomia-masterserver (npm start)?`
          : 'Falha ao alocar a sala.',
      )
    }
  }

  const avatarColors = useMemo(
    () => ({ body: hexToRgb(body), shirt: hexToRgb(shirt), pants: hexToRgb(pants) }),
    [body, shirt, pants],
  )

  const onlinePlayers = status?.players_online ?? 0
  const openRooms = status?.rooms ?? []

  return (
    <div className="loomia">
      <header className="topbar">
        <div className="brand">
          <span className="brand-logo">LOOMIA</span>
          <span className="brand-tag">build · play · share</span>
        </div>
        <nav className="topnav">
          <a href="#play">Jogar</a>
          <a href="#maps">Mapas</a>
          <a href="#rooms">Salas</a>
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
            <p className="eyebrow">site em node.js · cliente nativo em lovr (lua) · salas em go</p>
            <h1>
              Entre no mundo.
              <br />
              Construa como em 2008.
            </h1>
            <p className="lead">
              Loomia é um jogo online de mundos em blocos. O site é o lobby; ao clicar em
              “Jogar” o master server sobe uma sala autoritativa em Go e abre o cliente LOVR
              no seu computador pelo link <code>loomia://</code>.
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

              <label className="field compact">
                <span>Mapa</span>
                <select value={activeMapId} onChange={(event) => setSelectedMap(event.target.value)}>
                  {maps.length === 0 && <option value="">Nenhum mapa publicado</option>}
                  {maps.map((entry) => (
                    <option key={entry.id} value={entry.id}>
                      {entry.name}
                      {entry.room_open ? ` · ${entry.online ?? 0} online` : ''}
                    </option>
                  ))}
                </select>
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
                disabled={!status || maps.length === 0 || launchState === 'launching'}
              >
                <span className="play-icon">▶</span>
                {launchState === 'launching' ? 'ALOCANDO SALA…' : 'JOGAR NO MAPA'}
              </button>

              <div className={`launch-feedback ${launchState}`}>
                {launchState === 'idle' &&
                  'O cliente abre em uma janela própria pelo link loomia:// (o cliente LOVR precisa estar instalado).'}
                {launchState === 'launching' && <span>{launchMessage}</span>}
                {launchState === 'launched' && `✔ ${launchMessage}`}
                {launchState === 'error' && `✖ ${launchMessage}`}
                {status && (
                  <small>
                    handler <code>{status.protocol.scheme}</code>{' '}
                    {status.protocol.registered ? 'registrado no sistema' : 'ainda não registrado'} ·{' '}
                    <a href="/api/launch/info" target="_blank" rel="noreferrer">
                      detalhes
                    </a>
                  </small>
                )}
              </div>
            </div>
          </div>

          <div className="hero-visual">
            <WorldPreview map={map} />
            <div className="hero-visual-caption">
              <strong>{map?.name ?? 'Selecione um mapa'}</strong>
              <span>
                {maps.find((entry) => entry.id === map?.id)?.description ||
                  'Prévia do mapa servido pelo catálogo da comunidade.'}
              </span>
            </div>
          </div>
        </section>

        <section className="cards" id="maps">
          <article className="card">
            <h2>Mapas da comunidade</h2>
            <p>{maps.length} publicados</p>
            <ul className="player-list">
              {maps.length === 0 && <li className="muted">Nenhum mapa ainda — publique o primeiro!</li>}
              {maps.map((entry) => (
                <li key={entry.id}>
                  <span className="player-dot" />
                  <button type="button" className="link-button" onClick={() => setSelectedMap(entry.id)}>
                    {entry.name}
                  </button>
                  <small>
                    {entry.author} · {entry.online ? `${entry.online} online` : 'sala fechada'}
                  </small>
                </li>
              ))}
            </ul>
          </article>

          <article className="card" id="rooms">
            <h2>Salas ativas</h2>
            <p>{onlinePlayers} jogadores online</p>
            <ul className="player-list">
              {openRooms.length === 0 && <li className="muted">Nenhuma sala aberta — clique em Jogar!</li>}
              {openRooms.map((room) => (
                <li key={room.map_id}>
                  <span className="player-dot" />
                  {room.map_name}
                  <small>
                    {room.address} · {room.players}/{room.capacity}
                  </small>
                </li>
              ))}
            </ul>
          </article>

          <article className="card" id="about">
            <h2>Arquitetura</h2>
            <ul>
              <li>
                <strong>loomia-masterserver</strong> — Node.js: contas, catálogo, alocação de
                salas e API HTTP
              </li>
              <li>
                <strong>loomia-gameserver</strong> — Go: física autoritativa a 30 Hz e
                snapshots por UDP
              </li>
              <li>
                <strong>loomia-client-lovr</strong> — cliente 3D em Lua/LOVR, aberto pelo link{' '}
                <code>loomia://</code>
              </li>
              <li>
                <strong>loomia-frontend</strong> — este site (React + Vite)
              </li>
            </ul>
            <p className="muted">
              Sessões: {status?.sessions ?? 0} · tempo no ar:{' '}
              {status ? `${Math.floor(status.uptime_secs / 60)} min` : '—'}
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
