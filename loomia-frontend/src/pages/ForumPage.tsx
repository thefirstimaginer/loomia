// Fórum da rede: salas de discussão criadas por jogadores logados.
// Cada sala tem um título, uma descrição e um histórico de posts.

import { useCallback, useEffect, useState } from 'react'
import { AvatarPreview } from '../components/AvatarPreview'
import { RequireLogin } from '../components/RequireLogin'
import { api, type ForumPost, type ForumRoom } from '../lib/api'
import { Link, useRoute } from '../lib/router'
import { useSession } from '../lib/session'

export function ForumPage() {
  const { user, loading } = useSession()
  const [route] = useRoute()

  useEffect(() => {
    document.title = 'Fórum · Loomia'
  }, [])

  if (loading) return <main className="page"><p className="muted">Carregando…</p></main>
  if (!user) return <RequireLogin intention="participar do fórum" />

  const roomId = route.name === 'forumRoom' ? route.roomId : undefined
  return roomId ? <RoomView roomId={roomId} /> : <ForumList />
}

function ForumList() {
  const [rooms, setRooms] = useState<ForumRoom[]>([])
  const [loading, setLoading] = useState(true)
  const [creating, setCreating] = useState(false)
  const [title, setTitle] = useState('')
  const [description, setDescription] = useState('')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)

  const load = useCallback(async () => {
    setLoading(true)
    try {
      setRooms((await api.forumRooms()).rooms)
    } catch {
      setRooms([])
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => {
    void load()
  }, [load])

  const create = async (event: React.FormEvent<HTMLFormElement>) => {
    event.preventDefault()
    setBusy(true)
    setError(null)
    try {
      await api.createForumRoom({ title: title.trim(), description: description.trim() })
      setTitle('')
      setDescription('')
      setCreating(false)
      await load()
    } catch (problem) {
      setError(problem instanceof Error ? problem.message : 'Não foi possível criar a sala.')
    } finally {
      setBusy(false)
    }
  }

  return (
    <main className="page">
      <section className="page-head">
        <div>
          <p className="eyebrow">salas de discussão</p>
          <h1>Fórum</h1>
          <p className="lead">Crie uma sala sobre o que rola na rede e converse com outros Loomiers.</p>
        </div>
        <button className="launch-button" onClick={() => setCreating((open) => !open)}>
          + NOVA SALA
        </button>
      </section>

      {error && <p className="form-error">{error}</p>}

      {creating && (
        <section className="card">
          <div className="card-head">
            <h2>Nova sala de discussão</h2>
            <button className="subtle-link" onClick={() => setCreating(false)}>
              fechar
            </button>
          </div>
          <form onSubmit={create} className="map-form">
            <label className="field span-2">
              <span>Título da sala</span>
              <input value={title} maxLength={120} required onChange={(event) => setTitle(event.target.value)} />
            </label>
            <label className="field span-2">
              <span>Descrição</span>
              <textarea
                value={description}
                maxLength={500}
                style={{ minHeight: 90 }}
                placeholder="Sobre o que é essa conversa?"
                onChange={(event) => setDescription(event.target.value)}
              />
            </label>
            <div className="form-actions span-2">
              <button className="launch-button" type="submit" disabled={busy}>
                {busy ? 'CRIANDO…' : 'CRIAR SALA'}
              </button>
            </div>
          </form>
        </section>
      )}

      {loading ? (
        <p className="muted">Carregando salas…</p>
      ) : rooms.length === 0 ? (
        <div className="empty-state">
          <h3>Nenhuma sala ainda</h3>
          <p className="muted">Seja quem abre a primeira discussão da rede!</p>
        </div>
      ) : (
        <ul className="room-list">
          {rooms.map((room) => (
            <li key={room.id}>
              <Link to={`/forum/${room.id}`} className="room-card">
                <div>
                  <strong>{room.title}</strong>
                  <p className="muted small">{room.description || 'Sem descrição.'}</p>
                  <small className="muted">
                    por {room.authorName} · {room.replies} resposta{room.replies === 1 ? '' : 's'} · última atividade{' '}
                    {new Date(room.lastActivityAt).toLocaleDateString('pt-BR')}
                  </small>
                </div>
                <span className="pill">abrir →</span>
              </Link>
            </li>
          ))}
        </ul>
      )}
    </main>
  )
}

function RoomView({ roomId }: { roomId: string }) {
  const [posts, setPosts] = useState<ForumPost[]>([])
  const [text, setText] = useState('')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const { user } = useSession()

  const load = useCallback(async () => {
    try {
      setPosts((await api.forumPosts(roomId)).posts)
    } catch (problem) {
      setError(problem instanceof Error ? problem.message : 'Não foi possível carregar a sala.')
    }
  }, [roomId])

  useEffect(() => {
    void load()
  }, [load])

  const send = async (event: React.FormEvent<HTMLFormElement>) => {
    event.preventDefault()
    if (!text.trim()) return
    setBusy(true)
    try {
      await api.createForumPost(roomId, text.trim())
      setText('')
      await load()
    } catch (problem) {
      setError(problem instanceof Error ? problem.message : 'Não foi possível publicar.')
    } finally {
      setBusy(false)
    }
  }

  return (
    <main className="page">
      <p>
        <Link to="/forum" className="subtle-link">
          ⬅ voltar para as salas
        </Link>
      </p>

      {error && <p className="form-error">{error}</p>}

      <section className="card">
        <h2>Discussão</h2>
        {posts.length === 0 ? (
          <p className="muted">Nenhuma mensagem ainda — comece a conversa!</p>
        ) : (
          <ul className="forum-post-list">
            {posts.map((post) => (
              <li key={post.id}>
                {post.authorAvatar ? (
                  <AvatarPreview look={post.authorAvatar} size={56} />
                ) : (
                  <div className="avatar-placeholder" style={{ width: 52, height: 66 }} />
                )}
                <div>
                  <header>
                    <strong>{post.authorName}</strong>
                    <small className="muted">{new Date(post.createdAt).toLocaleString('pt-BR')}</small>
                  </header>
                  <p>{post.body}</p>
                </div>
              </li>
            ))}
          </ul>
        )}
      </section>

      <section className="card">
        <form onSubmit={send} className="inline-form">
          <input
            placeholder={user ? `Responder como ${user.nickname ?? user.firstName}…` : 'Responder…'}
            value={text}
            maxLength={2000}
            onChange={(event) => setText(event.target.value)}
          />
          <button className="launch-button small" type="submit" disabled={busy || !text.trim()}>
            ENVIAR
          </button>
        </form>
      </section>
    </main>
  )
}
