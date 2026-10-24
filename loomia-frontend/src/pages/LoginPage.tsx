// Login: email OU apelido + senha. A sessão fica em cookie HttpOnly.

import { useState } from 'react'
import { api } from '../lib/api'
import { Link } from '../lib/router'
import { useSession } from '../lib/session'

export function LoginPage({ onDone }: { onDone: (path: string) => void }) {
  const { setSession } = useSession()
  const [login, setLogin] = useState('')
  const [password, setPassword] = useState('')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)

  const submit = async (event: React.FormEvent) => {
    event.preventDefault()
    setBusy(true)
    setError(null)
    try {
      const result = await api.login({ login: login.trim(), password })
      setSession(result.user, result.avatars)
      onDone(result.hasAvatar ? '/explorar' : '/boas-vindas')
    } catch (problem) {
      setError(problem instanceof Error ? problem.message : 'Não foi possível entrar.')
    } finally {
      setBusy(false)
    }
  }

  return (
    <main className="page auth-page">
      <section className="auth-card">
        <p className="eyebrow">bem-vindo de volta</p>
        <h1>Entrar</h1>
        <p className="muted">Use seu email ou apelido.</p>

        <form onSubmit={(event) => void submit(event)} className="auth-form">
          <label className="field">
            <span>Email ou apelido</span>
            <input
              value={login}
              onChange={(event) => setLogin(event.target.value)}
              autoComplete="username"
              autoFocus
              required
            />
          </label>
          <label className="field">
            <span>Senha</span>
            <input
              type="password"
              value={password}
              onChange={(event) => setPassword(event.target.value)}
              autoComplete="current-password"
              required
            />
          </label>

          {error && <p className="form-error">{error}</p>}

          <button className="launch-button" type="submit" disabled={busy}>
            {busy ? 'ENTRANDO…' : 'ENTRAR'}
          </button>
        </form>

        <p className="auth-alt">
          Não tem conta? <Link to="/registrar">Criar uma agora</Link>
        </p>
      </section>
    </main>
  )
}
