// Amigos: pedidos recebidos (com a mensagem de apresentação), enviados,
// lista de amigos e busca de jogadores para adicionar.

import { useCallback, useEffect, useState } from 'react'
import { RequireLogin } from '../components/RequireLogin'
import { api, type Friend, type FriendRequest, type PlayerSummary } from '../lib/api'
import { Link, useRoute } from '../lib/router'
import { useSession } from '../lib/session'

export function FriendsPage() {
  const { user, loading } = useSession()
  if (loading) return <main className="page"><p className="muted">Carregando…</p></main>
  if (!user) return <RequireLogin intention="ver seus amigos" />
  return <FriendsContent />
}

function FriendsContent() {
  const [, navigate] = useRoute()
  const [friends, setFriends] = useState<Friend[]>([])
  const [requests, setRequests] = useState<FriendRequest[]>([])
  const [favorites, setFavorites] = useState<PlayerSummary[]>([])
  const [term, setTerm] = useState('')
  const [results, setResults] = useState<PlayerSummary[]>([])
  const [busy, setBusy] = useState(false)
  const [message, setMessage] = useState<string | null>(null)

  const load = useCallback(async () => {
    const [friendData, favoriteData] = await Promise.all([api.friends(), api.favorites()])
    setFriends(friendData.friends)
    setRequests(friendData.requests)
    setFavorites(favoriteData.favorites)
  }, [])

  useEffect(() => {
    void load().catch(() => setMessage('Não foi possível carregar seus amigos.'))
  }, [load])

  useEffect(() => {
    if (!term.trim()) {
      setResults([])
      return
    }
    const timer = setTimeout(() => {
      api
        .searchPlayers(term.trim())
        .then((result) => setResults(result.players))
        .catch(() => setResults([]))
    }, 250)
    return () => clearTimeout(timer)
  }, [term])

  const respond = async (request: FriendRequest, action: 'accept' | 'decline' | 'cancel') => {
    setBusy(true)
    try {
      if (action === 'accept') await api.acceptFriend(request.id)
      if (action === 'decline') await api.declineFriend(request.id)
      if (action === 'cancel') await api.cancelFriend(request.id)
      await load()
      setMessage(
        action === 'accept'
          ? `Agora você é amigo de ${request.fromName}.`
          : action === 'decline'
            ? 'Pedido recusado.'
            : 'Pedido cancelado.',
      )
    } catch (problem) {
      setMessage(problem instanceof Error ? problem.message : 'Falha ao responder')
    } finally {
      setBusy(false)
    }
  }

  const incoming = requests.filter((request) => request.direction === 'incoming' && request.status === 'pending')
  const outgoing = requests.filter((request) => request.direction === 'outgoing' && request.status === 'pending')

  return (
    <main className="page">
      <section className="page-head">
        <div>
          <p className="eyebrow">sua rede</p>
          <h1>Amigos</h1>
          <p className="lead">Pedidos, favoritos e busca de jogadores com o mesmo gosto pelo Loomia.</p>
        </div>
        <div className="search-box">
          <input
            type="search"
            placeholder="Buscar jogador pelo apelido…"
            value={term}
            onChange={(event) => setTerm(event.target.value)}
            aria-label="Buscar jogadores"
          />
        </div>
      </section>

      {message && <p className="form-success">{message}</p>}

      {results.length > 0 && (
        <section className="card">
          <h2>Resultados</h2>
          <ul className="people-list">
            {results.map((player) => (
              <li key={player.id}>
                <span className={`player-dot ${player.online ? 'online' : ''}`} />
                <button className="link-button" onClick={() => navigate(`/jogadores/${player.id}`)}>
                  {player.nickname}
                </button>
                <small>{player.bio || 'sem bio'}</small>
              </li>
            ))}
          </ul>
        </section>
      )}

      <div className="split-columns">
        <section className="card">
          <h2>Pedidos recebidos ({incoming.length})</h2>
          {incoming.length === 0 && <p className="muted">Nenhum pedido pendente.</p>}
          <ul className="request-list">
            {incoming.map((request) => (
              <li key={request.id}>
                <div>
                  <Link to={`/jogadores/${request.fromId}`} className="link-button">
                    {request.fromName}
                  </Link>
                  {request.message && <p className="request-message">“{request.message}”</p>}
                </div>
                <div className="form-actions">
                  <button className="launch-button small" onClick={() => void respond(request, 'accept')} disabled={busy}>
                    ACEITAR
                  </button>
                  <button className="ghost-button tiny" onClick={() => void respond(request, 'decline')} disabled={busy}>
                    Recusar
                  </button>
                </div>
              </li>
            ))}
          </ul>
        </section>

        <section className="card">
          <h2>Pedidos enviados ({outgoing.length})</h2>
          {outgoing.length === 0 && <p className="muted">Nenhum pedido aguardando resposta.</p>}
          <ul className="request-list">
            {outgoing.map((request) => (
              <li key={request.id}>
                <div>
                  <Link to={`/jogadores/${request.toId}`} className="link-button">
                    {request.toName}
                  </Link>
                  {request.message && <p className="request-message">“{request.message}”</p>}
                  <small className="muted">aguardando resposta</small>
                </div>
                <button className="ghost-button tiny" onClick={() => void respond(request, 'cancel')} disabled={busy}>
                  Cancelar
                </button>
              </li>
            ))}
          </ul>
        </section>
      </div>

      <section className="card">
        <h2>Seus amigos ({friends.length})</h2>
        {friends.length === 0 ? (
          <p className="muted">Sem amigos ainda — busque um jogador acima ou visite um mundo.</p>
        ) : (
          <ul className="people-list">
            {friends.map((friend) => (
              <li key={friend.id}>
                <span className="player-dot" />
                <Link to={`/jogadores/${friend.id}`} className="link-button">
                  {friend.nickname}
                </Link>
                <small>{friend.bio || 'sem bio'}</small>
              </li>
            ))}
          </ul>
        )}
      </section>

      <section className="card">
        <h2>Favoritos ({favorites.length})</h2>
        {favorites.length === 0 ? (
          <p className="muted">Você ainda não favoritou ninguém.</p>
        ) : (
          <ul className="people-list">
            {favorites.map((player) => (
              <li key={player.id}>
                <span className={`player-dot ${player.online ? 'online' : ''}`} />
                <Link to={`/jogadores/${player.id}`} className="link-button">
                  {player.nickname}
                </Link>
                <small>{player.online ? 'online' : 'offline'}</small>
              </li>
            ))}
          </ul>
        )}
      </section>
    </main>
  )
}
