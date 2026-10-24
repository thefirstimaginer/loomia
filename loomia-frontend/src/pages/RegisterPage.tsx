// Registrar: Nome, Sobrenome, Apelido, Email, Senha, Repetir Senha.
//
// O editor do PRIMEIRO avatar não fica aqui: depois do cadastro o jogador vai
// para a página de boas-vindas, que mostra o editor e então leva aos mundos.

import { useEffect, useState } from 'react'
import { api, type AvatarOptions } from '../lib/api'
import { Link } from '../lib/router'
import { useSession } from '../lib/session'

interface Fields {
  firstName: string
  lastName: string
  nickname: string
  email: string
  password: string
  passwordConfirm: string
}

const EMPTY: Fields = { firstName: '', lastName: '', nickname: '', email: '', password: '', passwordConfirm: '' }

export function RegisterPage({ onDone }: { onDone: (path: string) => void }) {
  const { setSession } = useSession()
  const [fields, setFields] = useState<Fields>(EMPTY)
  const [options, setOptions] = useState<AvatarOptions | undefined>(undefined)
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    api.avatarOptions().then(setOptions).catch(() => setOptions(undefined))
  }, [])

  const update = (key: keyof Fields) => (event: React.ChangeEvent<HTMLInputElement>) =>
    setFields((current) => ({ ...current, [key]: event.target.value }))

  const submit = async (event: React.FormEvent) => {
    event.preventDefault()
    setBusy(true)
    setError(null)
    try {
      const result = await api.register({
        firstName: fields.firstName.trim(),
        lastName: fields.lastName.trim(),
        nickname: fields.nickname.trim(),
        email: fields.email.trim(),
        password: fields.password,
        passwordConfirm: fields.passwordConfirm,
      })
      setSession(result.user, [])
      onDone('/boas-vindas')
    } catch (problem) {
      setError(problem instanceof Error ? problem.message : 'Não foi possível criar a conta.')
    } finally {
      setBusy(false)
    }
  }

  return (
    <main className="page auth-page">
      <section className="auth-card wide">
        <p className="eyebrow">nova conta</p>
        <h1>Criar conta</h1>
        <p className="muted">
          Depois do cadastro você monta seu primeiro avatar em segundos — e já vai direto para os mundos.
        </p>

        <form onSubmit={(event) => void submit(event)} className="auth-form grid-2" data-avatar-options={Boolean(options)}>
          <label className="field">
            <span>Nome</span>
            <input value={fields.firstName} onChange={update('firstName')} autoComplete="given-name" required />
          </label>
          <label className="field">
            <span>Sobrenome</span>
            <input value={fields.lastName} onChange={update('lastName')} autoComplete="family-name" required />
          </label>
          <label className="field">
            <span>Apelido</span>
            <input
              value={fields.nickname}
              onChange={update('nickname')}
              autoComplete="nickname"
              minLength={3}
              maxLength={20}
              pattern="[\w.\-]{3,20}"
              title="3 a 20 caracteres: letras, números, ponto, hífen e _"
              required
            />
          </label>
          <label className="field">
            <span>Email</span>
            <input type="email" value={fields.email} onChange={update('email')} autoComplete="email" required />
          </label>
          <label className="field">
            <span>Senha</span>
            <input
              type="password"
              value={fields.password}
              onChange={update('password')}
              autoComplete="new-password"
              minLength={8}
              required
            />
          </label>
          <label className="field">
            <span>Repetir senha</span>
            <input
              type="password"
              value={fields.passwordConfirm}
              onChange={update('passwordConfirm')}
              autoComplete="new-password"
              minLength={8}
              required
            />
          </label>

          {error && <p className="form-error span-2">{error}</p>}

          <button className="launch-button span-2" type="submit" disabled={busy}>
            {busy ? 'CRIANDO CONTA…' : 'CRIAR CONTA E ESCOLHER AVATAR'}
          </button>
        </form>

        <p className="auth-alt">
          Já tem conta? <Link to="/login">Entrar</Link>
        </p>
      </section>
    </main>
  )
}
