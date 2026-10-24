// Perfil: dados da conta + coleção de avatares (criar, editar, ativar, remover).

import { useEffect, useState } from 'react'
import { AvatarEditor, type AvatarEditorValue } from '../components/AvatarEditor'
import { AvatarPreview } from '../components/AvatarPreview'
import { RequireLogin } from '../components/RequireLogin'
import { api, type Avatar, type AvatarOptions, type User } from '../lib/api'
import { Link, useRoute } from '../lib/router'
import { useSession } from '../lib/session'

export function ProfilePage({ onNavigate }: { onNavigate: (path: string) => void }) {
  const { user, avatars, loading, refresh, setSession } = useSession()
  if (loading) return <main className="page"><p className="muted">Carregando…</p></main>
  if (!user) return <RequireLogin intention="ver seu perfil" />
  return (
    <ProfileContent
      user={user}
      avatars={avatars}
      refresh={refresh}
      setSession={setSession}
      onNavigate={onNavigate}
    />
  )
}

function ProfileContent({
  user,
  avatars,
  refresh,
  setSession,
  onNavigate,
}: {
  user: User
  avatars: Avatar[]
  refresh: () => Promise<void>
  setSession: (user: User | null, avatars?: Avatar[]) => void
  onNavigate: (path: string) => void
}) {
  const [options, setOptions] = useState<AvatarOptions | undefined>(undefined)
  const [editing, setEditing] = useState<Avatar | 'new' | null>(null)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [notice, setNotice] = useState<string | null>(null)
  const [bio, setBio] = useState(user.bio ?? '')
  const [savingBio, setSavingBio] = useState(false)
  const [unread, setUnread] = useState(0)
  const [requests, setRequests] = useState(0)
  const [, navigate] = useRoute()

  useEffect(() => {
    api.avatarOptions().then(setOptions).catch(() => setOptions(undefined))
    api
      .unread()
      .then((result) => setUnread(result.unread))
      .catch(() => setUnread(0))
    api
      .friends()
      .then((result) => setRequests(result.requests.filter((entry) => entry.direction === 'incoming' && entry.status === 'pending').length))
      .catch(() => setRequests(0))
  }, [])

  const active = avatars.find((avatar) => avatar.isActive) ?? avatars[0] ?? null

  const run = async (action: () => Promise<void>, successMessage: string) => {
    setBusy(true)
    setError(null)
    setNotice(null)
    try {
      await action()
      await refresh()
      setNotice(successMessage)
    } catch (problem) {
      setError(problem instanceof Error ? problem.message : 'Algo deu errado.')
    } finally {
      setBusy(false)
    }
  }

  const saveAvatar = async (value: AvatarEditorValue) => {
    if (editing === 'new') {
      await run(async () => {
        await api.createAvatar(value)
      }, 'Avatar criado.')
    } else if (editing) {
      await run(async () => {
        await api.updateAvatar(editing.id, value)
      }, 'Avatar atualizado.')
    }
    setEditing(null)
  }

  const logout = async () => {
    await api.logout().catch(() => undefined)
    setSession(null, [])
    onNavigate('/')
  }

  return (
    <main className="page profile-page">
      <section className="profile-head">
        <div className="profile-identity">
          {active && <AvatarPreview look={active} size={120} />}
          <div>
            <p className="eyebrow">minha conta</p>
            <h1>
              {user.firstName} {user.lastName}
            </h1>
            <p className="muted">
              <strong>@{user.nickname}</strong> · {user.email}
            </p>
            <p className="muted small">
              Conta criada em {new Date(user.createdAt).toLocaleDateString('pt-BR')}
              {active ? ` · avatar ativo: ${active.name}` : ''}
            </p>
          </div>
        </div>
        <div className="profile-actions">
          <Link to="/explorar" className="launch-button">
            ▶ EXPLORAR MUNDOS
          </Link>
          <Link to="/corner" className="ghost-button">
            Loomier&apos;s Corner
          </Link>
          <button type="button" className="ghost-button danger" onClick={() => void logout()}>
            Sair da conta
          </button>
        </div>
      </section>

      <section className="card">
        <div className="card-head">
          <div>
            <h2>Meus avatares</h2>
            <p className="muted">Você pode salvar vários e escolher qual usar ao entrar nos mundos.</p>
          </div>
          <button type="button" className="launch-button small" onClick={() => setEditing('new')} disabled={busy}>
            + NOVO AVATAR
          </button>
        </div>

        {notice && <p className="form-success">{notice}</p>}
        {error && <p className="form-error">{error}</p>}

        <div className="avatar-grid">
          {avatars.map((avatar) => (
            <article key={avatar.id} className={`avatar-card ${avatar.isActive ? 'active' : ''}`}>
              <AvatarPreview look={avatar} size={110} />
              <h3>{avatar.name}</h3>
              {avatar.isActive ? (
                <span className="pill live">em uso</span>
              ) : (
                <button
                  type="button"
                  className="ghost-button tiny"
                  disabled={busy}
                  onClick={() =>
                    void run(async () => {
                      await api.activateAvatar(avatar.id)
                    }, `"${avatar.name}" agora é o seu avatar ativo.`)
                  }
                >
                  Usar este
                </button>
              )}
              <div className="avatar-card-actions">
                <button type="button" className="subtle-link" onClick={() => setEditing(avatar)} disabled={busy}>
                  Editar
                </button>
                <button
                  type="button"
                  className="subtle-link danger"
                  disabled={busy || avatars.length <= 1}
                  title={avatars.length <= 1 ? 'Você precisa de pelo menos um avatar' : 'Remover avatar'}
                  onClick={() =>
                    void run(async () => {
                      await api.deleteAvatar(avatar.id)
                    }, `"${avatar.name}" foi removido.`)
                  }
                >
                  Remover
                </button>
              </div>
            </article>
          ))}

          {avatars.length === 0 && (
            <div className="empty-state">
              <h3>Nenhum avatar salvo</h3>
              <p className="muted">Crie o primeiro para poder entrar nos mundos.</p>
            </div>
          )}
        </div>
      </section>

      {editing && (
        <section className="card editor-card">
          <div className="card-head">
            <h2>{editing === 'new' ? 'Novo avatar' : `Editando "${editing.name}"`}</h2>
          </div>
          <AvatarEditor
            options={options}
            initial={editing === 'new' ? null : editing}
            defaultName={editing === 'new' ? `Avatar ${avatars.length + 1}` : editing.name}
            submitLabel={editing === 'new' ? 'CRIAR AVATAR' : 'SALVAR ALTERAÇÕES'}
            busy={busy}
            error={error}
            onSubmit={saveAvatar}
            onCancel={() => setEditing(null)}
          />
        </section>
      )}

      <div className="split-columns">
        <section className="card">
          <h2>Sobre você</h2>
          <label className="field">
            <span>Bio (aparece no seu perfil público)</span>
            <textarea
              value={bio}
              maxLength={500}
              placeholder="Conte um pouco sobre você e seus mundos…"
              onChange={(event) => setBio(event.target.value)}
              style={{ minHeight: 90 }}
            />
          </label>
          <div className="form-actions">
            <button
              className="ghost-button"
              disabled={savingBio || bio === (user.bio ?? '')}
              onClick={() => {
                setSavingBio(true)
                api
                  .setBio(bio.trim())
                  .then(() => setNotice('Bio atualizada.'))
                  .catch((problem) => setError(problem instanceof Error ? problem.message : 'Falha ao salvar a bio'))
                  .finally(() => setSavingBio(false))
              }}
            >
              {savingBio ? 'SALVANDO…' : 'SALVAR BIO'}
            </button>
          </div>
        </section>

        <section className="card">
          <h2>Social</h2>
          <ul className="people-list">
            <li>
              <button className="link-button" onClick={() => navigate('/amigos')}>
                Amigos e pedidos
              </button>
              <small>{requests > 0 ? `${requests} pedido(s) esperando` : 'tudo em dia'}</small>
            </li>
            <li>
              <button className="link-button" onClick={() => navigate('/amigos')}>
                Mensagens não lidas
              </button>
              <small>{unread}</small>
            </li>
            <li>
              <button className="link-button" onClick={() => navigate('/tickets')}>
                Suporte e sugestões
              </button>
              <small>abrir chamado</small>
            </li>
          </ul>
        </section>
      </div>

      <section className="card">
        <h2>Meus mundos</h2>
        <p className="muted">Veja o que você publicou na Loomier&apos;s Corner.</p>
        <button type="button" className="ghost-button" onClick={() => navigate('/corner')}>
          Abrir a Corner
        </button>
      </section>
    </main>
  )
}
