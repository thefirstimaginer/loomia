// Boas-vindas: o primeiro avatar.
//
// É a página para onde o registro manda o jogador. Ao salvar o primeiro avatar,
// ele segue para Explorar Mundos (como pedido no produto).

import { useEffect, useState } from 'react'
import { AvatarEditor, type AvatarEditorValue } from '../components/AvatarEditor'
import { api, type AvatarOptions } from '../lib/api'
import { useSession } from '../lib/session'
import { RequireLogin } from '../components/RequireLogin'

export function WelcomePage({ onDone }: { onDone: (path: string) => void }) {
  const { user, loading, refresh } = useSession()
  const [options, setOptions] = useState<AvatarOptions | undefined>(undefined)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    api.avatarOptions().then(setOptions).catch(() => setOptions(undefined))
  }, [])

  if (loading) return <main className="page"><p className="muted">Carregando…</p></main>
  if (!user) return <RequireLogin intention="continuar" />

  const save = async (value: AvatarEditorValue) => {
    setBusy(true)
    setError(null)
    try {
      await api.createAvatar({ ...value, isActive: true })
      await refresh()
      onDone('/explorar')
    } catch (problem) {
      setError(problem instanceof Error ? problem.message : 'Não foi possível salvar o avatar.')
    } finally {
      setBusy(false)
    }
  }

  return (
    <main className="page">
      <section className="welcome-banner">
        <p className="eyebrow">seja bem-vindo</p>
        <h1>
          Olá, {user.firstName}! <span aria-hidden>👋</span>
        </h1>
        <p className="lead">
          Sua conta está pronta. Monte seu <strong>primeiro avatar</strong> — você pode salvar outros depois no seu
          perfil.
        </p>
      </section>

      <section className="card editor-card">
        <h2>Seu primeiro avatar</h2>
        <AvatarEditor
          options={options}
          defaultName={`Avatar de ${user.nickname}`}
          submitLabel="SALVAR E IR PARA OS MUNDOS"
          busy={busy}
          error={error}
          onSubmit={save}
        />
      </section>
    </main>
  )
}
