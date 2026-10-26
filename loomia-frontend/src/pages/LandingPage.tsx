// Landing: apresenta o Loomia, os mundos em destaque e o convite para criar conta.

import { useEffect, useState } from 'react'
import { WorldPreview } from '../WorldPreview'
import { api, type MapInfo, type WorldMap } from '../lib/api'
import { Link } from '../lib/router'
import { useSession } from '../lib/session'

export function LandingPage() {
  const { user } = useSession()
  const [maps, setMaps] = useState<MapInfo[]>([])
  const [preview, setPreview] = useState<WorldMap | null>(null)
  const [online, setOnline] = useState(0)

  useEffect(() => {
    api
      .maps()
      .then((list) => {
        setMaps(list)
        if (list[0]) {
          void api
            .map(list[0].id)
            .then(setPreview)
            .catch(() => setPreview(null))
        }
      })
      .catch(() => setMaps([]))
    api
      .status()
      .then((status) => setOnline(status.players_online))
      .catch(() => setOnline(0))
  }, [])

  return (
    <main>
      <section className="hero">
        <div className="hero-copy">
          <p className="eyebrow">mundos em blocos · multiplayer · feito pela comunidade</p>
          <h1>
            Construa um mundo e jogue com amigos, como em 2008!
          </h1>
          <p className="lead">
            Loomia é um jogo online de mundos em blocos. Explore mapas criados pela comunidade, jogue com amigos
            e publique seus próprios mundos na <strong>Loomier&apos;s Corner</strong>.
          </p>

          <div className="hero-actions">
            {user ? (
              <>
                <Link to="/explorar" className="launch-button">
                  EXPLORAR MUNDOS
                </Link>
                <Link to="/perfil" className="ghost-button">
                  Meu perfil
                </Link>
              </>
            ) : (
              <>
                <Link to="/registrar" className="launch-button">
                  CRIAR MINHA CONTA
                </Link>
                <Link to="/login" className="ghost-button">
                  Já tenho conta
                </Link>
              </>
            )}
          </div>

          <ul className="hero-stats">
            <li>
              <strong>{maps.length}</strong> mundos publicados
            </li>
            <li>
              <strong>{online}</strong> jogadores agora
            </li>
            <li>
              <strong>30 Hz</strong> de física autoritativa
            </li>
          </ul>
        </div>

        <div className="hero-visual">
          <WorldPreview map={preview} />
          <div className="hero-visual-caption">
            <strong>{preview?.name ?? 'Baseplate Clássica'}</strong>
            <span>{maps[0]?.description || 'Um dos mundos servidos pela comunidade.'}</span>
          </div>
        </div>
      </section>

      <section className="cards">
        <article className="card">
          <h2>Como funciona</h2>
          <ol className="steps">
            <li>
              <strong>Crie sua conta</strong> — email (ou apelido) e senha.
            </li>
            <li>
              <strong>Monte seu avatar</strong> — pele, camisa, calça e chapéu. Salve vários no perfil.
            </li>
            <li>
              <strong>Explore Mundos</strong> — pesquise pelo nome, autor ou descrição.
            </li>
            <li>
              <strong>Baixe o cliente</strong> — Se divirta e convide seus amigos!
            </li>
          </ol>
        </article>

        <article className="card">
          <h2>Mundos em destaque</h2>
          {maps.length === 0 ? (
            <p className="muted">Nenhum mundo publicado ainda — seja o primeiro na Loomier&apos;s Corner.</p>
          ) : (
            <ul className="player-list">
              {maps.slice(0, 5).map((map) => (
                <li key={map.id}>
                  <span className="player-dot" />
                  <Link to="/explorar" className="link-button">
                    {map.name}
                  </Link>
                  <small>
                    {map.author} · {map.blocks} blocos
                  </small>
                </li>
              ))}
            </ul>
          )}
        </article>

        <article className="card">
          <h2>Loomier&apos;s Club</h2>
          <p className="muted">
            O espaço de quem cria. Publique seu mapa, acompanhe quantas pessoas entraram e atualize o mundo quando
            quiser.
          </p>
          <Link to="/club" className="ghost-button">
            Entrar no Clube
          </Link>
        </article>
      </section>
    </main>
  )
}
