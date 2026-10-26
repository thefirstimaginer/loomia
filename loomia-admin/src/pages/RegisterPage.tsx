// Criação de conta de administrador.
//
// Só aparece para quem já está logado como admin no painel. O captcha mantém
// bots longe do formulário mesmo que alguém deixe a sessão aberta.

import { useState } from 'react'
import { Captcha, useCaptcha } from '../components/Captcha'
import { adminApi, type AdminUser } from '../lib/api'

export function RegisterPage({ onCreated }: { onCreated: (admin: AdminUser) => void }) {
  const [form, setForm] = useState({
    firstName: '',
    lastName: '',
    nickname: '',
    email: '',
    password: '',
    passwordConfirm: '',
  })
  const { challenge, refresh } = useCaptcha()
  const [captchaAnswer, setCaptchaAnswer] = useState('')
  const [busy, setBusy] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [success, setSuccess] = useState<string | null>(null)

  const set = (key: keyof typeof form) => (event: React.ChangeEvent<HTMLInputElement>) =>
    setForm((prev) => ({ ...prev, [key]: event.target.value }))

  const refreshCaptcha = () => {
    setCaptchaAnswer('')
    void refresh()
  }

  const submit = async (event: React.FormEvent<HTMLFormElement>) => {
    event.preventDefault()
    if (!challenge) {
      setError('Captcha ainda não carregou — tente de novo.')
      return
    }
    setBusy(true)
    setError(null)
    try {
      const result = await adminApi.createAdmin({
        ...form,
        firstName: form.firstName.trim(),
        lastName: form.lastName.trim(),
        nickname: form.nickname.trim(),
        email: form.email.trim(),
        captchaToken: challenge.token,
        captchaAnswer: captchaAnswer.trim().toUpperCase(),
      })
      setSuccess(`Conta de admin criada para ${result.user.email}.`)
      setForm({ firstName: '', lastName: '', nickname: '', email: '', password: '', passwordConfirm: '' })
      refreshCaptcha()
      onCreated(result.user)
    } catch (problem) {
      setError(problem instanceof Error ? problem.message : 'Não foi possível criar a conta.')
      refreshCaptcha()
    } finally {
      setBusy(false)
    }
  }

  return (
    <section className="panel register-panel">
      <div className="panel-head">
        <div>
          <h2>Criar conta de admin</h2>
          <p className="muted small">A nova conta já nasce com papel <code>admin</code> e pode acessar este painel.</p>
        </div>
      </div>

      <form onSubmit={(event) => void submit(event)} className="register-form">
        <div className="register-grid">
          <label>
            <span>Nome</span>
            <input value={form.firstName} onChange={set('firstName')} required autoComplete="given-name" />
          </label>
          <label>
            <span>Sobrenome</span>
            <input value={form.lastName} onChange={set('lastName')} required autoComplete="family-name" />
          </label>
          <label>
            <span>Apelido</span>
            <input value={form.nickname} onChange={set('nickname')} required />
          </label>
          <label>
            <span>Email</span>
            <input type="email" value={form.email} onChange={set('email')} required autoComplete="email" />
          </label>
          <label>
            <span>Senha</span>
            <input type="password" value={form.password} onChange={set('password')} required minLength={8} autoComplete="new-password" />
          </label>
          <label>
            <span>Confirmar senha</span>
            <input
              type="password"
              value={form.passwordConfirm}
              onChange={set('passwordConfirm')}
              required
              minLength={8}
              autoComplete="new-password"
            />
          </label>
        </div>

        <div className="field-captcha">
          <span className="captcha-label">Verificação (captcha)</span>
          <Captcha challenge={challenge} value={captchaAnswer} onChange={setCaptchaAnswer} onRefresh={refreshCaptcha} />
        </div>

        {error && <p className="notice error">{error}</p>}
        {success && <p className="notice ok">{success}</p>}

        <div className="actions">
          <button className="button primary" type="submit" disabled={busy}>
            {busy ? 'Criando…' : 'Criar conta admin'}
          </button>
        </div>
      </form>
    </section>
  )
}
