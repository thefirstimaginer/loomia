// Explorar Mundos: catálogo com busca. Só com login (regra do produto).

import { useCallback, useEffect, useMemo, useState } from 'react'
import { api, type MapInfo, type WorldMap } from '../lib/api'
import { WorldPreview } from '../WorldPreview'
import { Link, useRoute } from '../lib/router'
import { useSession } from '../lib/session'
import { RequireLogin } from '../components/RequireLogin'

export function ExplorePage() {
  const { user, loading } = useSession()
  const [route, navigate] = useRoute()
  const selected = route.name === 'play' ? route.mapId : ''

  if (loading) return <main className="page"><p className="muted">Carregando…</p></main>
  if (!user) return <RequireLogin intention="explorar os mundos" />

  return <ExploreCatalog selected={selected} onSelect={(id) => navigate(`/jogar/${id}`)} />
}

function ExploreCatalog({ selected, onSelect }: { selected: string; onSelect: (id: string) => void }) {
  const [search, setSearch] = useState('')
  const [maps, setMaps] = useState<MapInfo[]>([])
  const [loading, setLoading] = useState(true)
  const [world, setWorld] = useState<WorldMap | null>(null)
  const [launching, setLaunching] = useState(false)
  const [message, setMessage] = useState<{ kind: 'ok' | 'error'; text: string } | null>(null)

  const load = useCallback(async (term: string) => {
    setLoading(true)
    try {
      setMaps(await api.maps(term))
    } catch {
      setMaps([])
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => {
    const timer = setTimeout(() => void load(search), 250)
    return () => clearTimeout(timer)
  }, [search, load])

  useEffect(() => {
    if (!selected) {
      setWorld(null)
      return
    }
    api
      .map(selected)
      .then(setWorld)
      .catch(() => setWorld(null))
  }, [selected])

  const active = useMemo(() => maps.find((map) => map.id === selected) ?? null, [maps, selected])

  const play = async () => {
    if (!selected) return
    setLaunching(true)
    setMessage(null)
    try {
      const result = await api.launch(selected)
      setMessage({ kind: 'ok', text: `${result.message}. Abrindo o cliente Loomia…` })
      window.location.href = result.launch_url
    } catch (error) {
      setMessage({
        kind: 'error',
        text:
          error instanceof Error
            ? `${error.message} — instale o cliente na página de Download se ainda não tiver.`
            : 'Não foi possível abrir a sala.',
      })
    } finally {
      setLaunching(false)
    }
  }

  return (
    <main className="page explore-page">
      <section className="page-head">
        <div>
          <p className="eyebrow">catálogo da comunidade</p>
          <h1>Explorar Mundos</h1>
          <p className="lead">Encontre um mundo, veja a prévia e entre na sala com um clique.</p>
        </div>
        <div className="search-box">
          <input
            type="search"
            value={search}
            placeholder="Pesquisar por nome, autor ou descrição…"
            onChange={(event) => setSearch(event.target.value)}
            aria-label="Pesquisar mapas"
          />
          <span className="search-count">{loading ? '…' : `${maps.length} mundos`}</span>
        </div>
      </section>

      <div className="explore-layout">
        <div className="map-grid">
          {!loading && maps.length === 0 && (
            <div className="empty-state">
              <h3>Nenhum mundo encontrado</h3>
              <p className="muted">Tente outra busca ou publique o primeiro mundo na Loomier&apos;s Corner.</p>
              <Link to="/corner" className="ghost-button">
                Ir para a Corner
              </Link>
            </div>
          )}
          {maps.map((map) => (
            <button
              key={map.id}
              type="button"
              className={`map-card ${selected === map.id ? 'selected' : ''}`}
              onClick={() => onSelect(map.id)}
            >
              <div className="map-card-top">
                <strong>{map.name}</strong>
                {map.room_open ? <span className="pill live">{map.online} online</span> : <span className="pill">sala fechada</span>}
              </div>
              <p className="map-card-desc">{map.description || 'Sem descrição.'}</p>
              <footer>
                <span>{map.author}</span>
                <span>
                  {map.blocks} blocos · até {map.maxPlayers}
                </span>
              </footer>
            </button>
          ))}
        </div>

        <aside className="map-detail">
          {world ? (
            <>
              <WorldPreview map={world} />
              <h2>{world.name}</h2>
              <p className="muted">{active?.description || 'Prévia do mundo selecionado.'}</p>
              <button className="launch-button" onClick={() => void play()} disabled={launching}>
                {launching ? 'ABRINDO SALA…' : '▶ ENTRAR NO MUNDO'}
              </button>
              {message && <p className={`form-${message.kind === 'ok' ? 'success' : 'error'}`}>{message.text}</p>}
              <Link to="/download" className="subtle-link">
                Ainda não tem o cliente? Baixe aqui
              </Link>
            </>
          ) : (
            <div className="detail-placeholder">
              <h2>Escolha um mundo</h2>
              <p className="muted">Selecione um card à esquerda para ver a prévia e entrar.</p>
            </div>
          )}
        </aside>
      </div>
    </main>
  )
}
