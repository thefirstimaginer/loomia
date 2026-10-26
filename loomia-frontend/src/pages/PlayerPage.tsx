// Perfil de outro jogador: dados, mundos, avatar, favoritar, pedido de amizade
// (com mensagem que chega ao outro mesmo antes do aceite) e reportar.

import { useCallback, useEffect, useState } from 'react'
import { AvatarPreview } from '../components/AvatarPreview'
import { RequireLogin } from '../components/RequireLogin'
import { api, type ChatMessage, type PublicProfile } from '../lib/api'
import { Link, useRoute, worldPath } from '../lib/router'
import { useSession } from '../lib/session'

export function PlayerPage() {
  const { user, loading } = useSession()
  const [route] = useRoute()
  const playerId = route.name === 'player' ? route.playerId : ''

  // Hooks nunca podem ser condicionais: os early returns vêm depois deles.
  useEffect(() => {
    document.title = 'Jogador · Loomia'
  }, [])

  if (loading) return <main className="page"><p className="muted">Carregando…</p></main>
  if (!user) return <RequireLogin intention="ver jogadores" />
  return <PlayerProfile playerId={playerId} />
}

function PlayerProfile({ playerId }: { playerId: string }) {
  const [profile, setProfile] = useState<PublicProfile | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [busy, setBusy] = useState(false)
  const [notice, setNotice] = useState<string | null>(null)

  // Ações que abrem um painel lateral.
  const [mode, setMode] = useState<'none' | 'friend' | 'report' | 'chat'>('none')
  const [friendMessage, setFriendMessage] = useState('')
  const [reportText, setReportText] = useState('')
  const [chat, setChat] = useState<ChatMessage[]>([])
  const [chatText, setChatText] = useState('')

  const load = useCallback(async () => {
    try {
      setProfile(await api.player(playerId))
      setError(null)
    } catch (problem) {
      setError(problem instanceof Error ? problem.message : 'Não foi possível carregar o perfil')
    }
  }, [playerId])

  useEffect(() => {
    void load()
  }, [load])

  const run = async (action: () => Promise<void>, success: string) => {
    setBusy(true)
    setError(null)
    try {
      await action()
      setNotice(success)
      await load()
    } catch (problem) {
      setError(problem instanceof Error ? problem.message : 'Algo deu errado')
    } finally {
      setBusy(false)
    }
  }

  const openChat = async () => {
    setMode('chat')
    try {
      setChat((await api.messages(playerId)).messages)
    } catch (problem) {
      setError(problem instanceof Error ? problem.message : 'Falha ao abrir a conversa')
    }
  }

  if (error && !profile) return <main className="page"><p className="form-error">{error}</p></main>
  if (!profile) return <main className="page"><p className="muted">Carregando perfil…</p></main>

  const name = profile.nickname ?? `${profile.firstName} ${profile.lastName}`
  const avatar = profile.avatars.find((entry) => entry.isActive) ?? profile.avatars[0] ?? null

  return (
    <main className="page profile-page">
      <section className="profile-head">
        <div className="profile-identity">
          {avatar ? <AvatarPreview look={avatar} size={120} /> : <div className="avatar-placeholder" />}
          <div>
            <p className="eyebrow">jogador</p>
            <h1>{name}</h1>
            <p className="muted">
              {profile.firstName} {profile.lastName} · entrou em{' '}
              {new Date(profile.createdAt).toLocaleDateString('pt-BR')}
            </p>
            <p className="muted">
              {profile.mapsCount} mundo(s) publicado(s) · {profile.friends} amigo(s) · {profile.favorites} favorito(s)
            </p>
          </div>
        </div>

        {!profile.isMe && (
          <div className="profile-actions">
            {profile.isFriend ? (
              <button className="ghost-button" onClick={() => void openChat()} disabled={busy}>
                💬 Enviar mensagem
              </button>
            ) : profile.hasPendingRequest ? (
              <span className="pill">
                {profile.pendingRequestFromMe ? 'pedido de amizade enviado' : 'pedido recebido — responda em Amigos'}
              </span>
            ) : (
              <button className="launch-button small" onClick={() => setMode('friend')} disabled={busy}>
                + ADICIONAR AMIGO
              </button>
            )}

            <button
              className="ghost-button"
              onClick={() =>
                void run(
                  async () => {
                    if (profile.isFavorite) await api.unfavorite(playerId)
                    else await api.favorite(playerId)
                  },
                  profile.isFavorite ? 'Removido dos favoritos.' : 'Adicionado aos favoritos.',
                )
              }
              disabled={busy}
            >
              {profile.isFavorite ? '★ Favoritado' : '☆ Favoritar'}
            </button>

            <button className="ghost-button danger" onClick={() => setMode('report')} disabled={busy}>
              ⚠ Reportar
            </button>
          </div>
        )}
      </section>

      {notice && <p className="form-success">{notice}</p>}
      {error && <p className="form-error">{error}</p>}

      {profile.bio && (
        <section className="card">
          <h2>Sobre</h2>
          <p>{profile.bio}</p>
        </section>
      )}

      {mode === 'friend' && (
        <section className="card">
          <div className="card-head">
            <h2>Pedido de amizade</h2>
            <button className="subtle-link" onClick={() => setMode('none')}>
              fechar
            </button>
          </div>
          <p className="muted">
            Escreva uma mensagem de apresentação. Ela chega junto com o pedido — mesmo antes de {name} aceitar.
          </p>
          <label className="field">
            <span>Mensagem (opcional)</span>
            <textarea
              value={friendMessage}
              maxLength={500}
              placeholder="Oi! Vi seu mundo e queria jogar junto."
              onChange={(event) => setFriendMessage(event.target.value)}
              style={{ minHeight: 100 }}
            />
          </label>
          <div className="form-actions">
            <button
              className="launch-button"
              disabled={busy}
              onClick={() =>
                void run(async () => {
                  await api.sendFriendRequest(playerId, friendMessage)
                  setMode('none')
                  setFriendMessage('')
                }, 'Pedido enviado!')
              }
            >
              ENVIAR PEDIDO
            </button>
          </div>
        </section>
      )}

      {mode === 'chat' && (
        <section className="card">
          <div className="card-head">
            <h2>Conversa com {name}</h2>
            <button className="subtle-link" onClick={() => setMode('none')}>
              fechar
            </button>
          </div>
          <ul className="chat-list">
            {chat.length === 0 && <li className="muted">Nenhuma mensagem ainda — diga oi!</li>}
            {chat.map((message) => (
              <li key={message.id} className={message.fromId === profile.id ? 'them' : 'me'}>
                <p>{message.body}</p>
                <small className="muted">{new Date(message.createdAt).toLocaleString('pt-BR')}</small>
              </li>
            ))}
          </ul>
          <form
            className="inline-form"
            onSubmit={(event) => {
              event.preventDefault()
              if (!chatText.trim()) return
              void run(async () => {
                await api.sendMessage(playerId, chatText.trim())
                setChatText('')
                setChat((await api.messages(playerId)).messages)
              }, '')
            }}
          >
            <input placeholder="Escreva uma mensagem…" value={chatText} onChange={(event) => setChatText(event.target.value)} />
            <button className="launch-button small" type="submit" disabled={busy}>
              ENVIAR
            </button>
          </form>
        </section>
      )}

      {mode === 'report' && (
        <section className="card">
          <div className="card-head">
            <h2>Reportar {name}</h2>
            <button className="subtle-link" onClick={() => setMode('none')}>
              fechar
            </button>
          </div>
          <p className="muted">
            Descreva o que aconteceu. A equipe do Loomia recebe o report com o seu nome, o nome de {name} e a descrição.
          </p>
          <label className="field">
            <span>Descrição</span>
            <textarea
              value={reportText}
              minLength={10}
              maxLength={2000}
              placeholder="Ex.: ele usou linguagem ofensiva no chat do mundo X."
              onChange={(event) => setReportText(event.target.value)}
              style={{ minHeight: 120 }}
            />
          </label>
          <div className="form-actions">
            <button
              className="launch-button"
              disabled={busy || reportText.trim().length < 10}
              onClick={() =>
                void run(async () => {
                  await api.report({ targetId: playerId, description: reportText.trim() })
                  setMode('none')
                  setReportText('')
                }, 'Report enviado para a equipe. Obrigado!')
              }
            >
              ENVIAR REPORT
            </button>
          </div>
        </section>
      )}

      <section className="card">
        <h2>Mundos de {name}</h2>
        {profile.maps.length === 0 ? (
          <p className="muted">Este jogador ainda não publicou mundos.</p>
        ) : (
          <ul className="player-list">
            {profile.maps.map((map) => (
              <li key={map.id}>
                <span className="player-dot" />
                <Link to={worldPath(map.id, map.name)} className="link-button">
                  {map.name}
                </Link>
                <small>{map.blocks} blocos</small>
              </li>
            ))}
          </ul>
        )}
      </section>
    </main>
  )
}
