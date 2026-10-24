// Login do painel: valida credenciais e confere se a conta é admin.
//
// Jogador comum recebe "sem acesso" — o painel nunca mostra dados a quem não é
// da equipe (o backend também bloqueia: defesa em profundidade).

import { useState } from 'react'
import { adminApi, type AdminUser } from '../lib/api'

export function LoginPage({ onLogin }: { onLogin: (admin: AdminUser) => void }) {
  const [login, setLogin] = useState('')
  const [password, setPassword] = useState('')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)

  const submit = async (event: React.FormEvent) => {
    event.preventDefault()
    setBusy(true)
    setError(null)
    try {
      const session = await adminApi.login({ login: login.trim(), password })
      if (session.user.role !== 'admin') {
        await adminApi.logout()
        throw new Error('esta conta não é de administrador')
      }
      onLogin(session.user)
    } catch (problem) {
      setError(problem instanceof Error ? problem.message : 'Não foi possível entrar')
    } finally {
      setBusy(false)
    }
  }

  return (
    <main className="login-screen">
      <form className="panel login-card" onSubmit={(event) => void submit(event)}>
        <p className="eyebrow">loomia · administração</p>
        <h1>Painel de Admin</h1>
        <p className="muted">Entrar com credenciais de administrador.</p>

        <label>
          <span>Email ou apelido</span>
          <input value={login} onChange={(event) => setLogin(event.target.value)} autoComplete="username" autoFocus required />
        </label>
        <label>
          <span>Senha</span>
          <input
            type="password"
            value={password}
            onChange={(event) => setPassword(event.target.value)}
            autoComplete="current-password"
            required
          />
        </label>

        {error && <p className="notice error">{error}</p>}

        <button className="button primary" type="submit" disabled={busy}>
          {busy ? 'Entrando…' : 'Entrar'}
        </button>
        <p className="muted small">
          Contas admin são definidas em <code>LOOMIA_ADMIN_EMAILS</code> no servidor.
        </p>
      </form>
    </main>
  )
}
