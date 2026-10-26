// Explorar Mundos: catálogo com busca. Cada card leva à página do mundo.
// Só com login (regra do produto).

import { useCallback, useEffect, useState } from 'react'
import { api, type MapInfo } from '../lib/api'
import { Link, worldPath } from '../lib/router'
import { useSession } from '../lib/session'
import { RequireLogin } from '../components/RequireLogin'

export function ExplorePage() {
  const { user, loading } = useSession()

  useEffect(() => {
    document.title = 'Mundos · Loomia'
  }, [])

  if (loading) return <main className="page"><p className="muted">Carregando…</p></main>
  if (!user) return <RequireLogin intention="explorar os mundos" />

  return <ExploreCatalog />
}

function ExploreCatalog() {
  const [search, setSearch] = useState('')
  const [maps, setMaps] = useState<MapInfo[]>([])
  const [loading, setLoading] = useState(true)

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

  return (
    <main className="page explore-page">
      <section className="page-head">
        <div>
          <p className="eyebrow">catálogo da comunidade</p>
          <h1>Mundos</h1>
          <p className="lead">Encontre um mundo e abra a página dele para ver a prévia, o criador e entrar na sala.</p>
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

      <div className="map-grid">
        {!loading && maps.length === 0 && (
          <div className="empty-state">
            <h3>Nenhum mundo encontrado</h3>
            <p className="muted">Tente outra busca ou publique o primeiro mundo no Loomier&apos;s Club.</p>
            <Link to="/club" className="ghost-button">
              Ir para o Club
            </Link>
          </div>
        )}
        {maps.map((map) => (
          <Link key={map.id} to={worldPath(map.id, map.name)} className={`map-card ${map.room_open ? 'live' : ''}`}>
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
          </Link>
        ))}
      </div>
    </main>
  )
}
