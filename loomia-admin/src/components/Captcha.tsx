// Captcha do painel: o servidor gera o código e o SVG distorcido.
//
// A resposta só existe no servidor — o cliente recebe token + imagem e envia
// o que o usuário digitou. Sem o par válido, /api/admin/register recusa.

import { adminApi as api } from '../lib/api'
import { useEffect, useState } from 'react'

export interface CaptchaChallenge {
  token: string
  svg: string
}

export function useCaptcha(): {
  challenge: CaptchaChallenge | null
  loading: boolean
  refresh: () => void
} {
  const [challenge, setChallenge] = useState<CaptchaChallenge | null>(null)
  const [loading, setLoading] = useState(true)

  useEffect(() => {
    let alive = true
    setLoading(true)
    api
      .captcha()
      .then((result: { token: string; svg: string }) => {
        if (alive) setChallenge(result)
      })
      .catch(() => {
        if (alive) setChallenge(null)
      })
      .finally(() => {
        if (alive) setLoading(false)
      })
    return () => {
      alive = false
    }
  }, [])

  const refresh = () => {
    api
      .captcha()
      .then(setChallenge)
      .catch(() => setChallenge(null))
  }

  return { challenge, loading, refresh }
}

export function Captcha({
  challenge,
  value,
  onChange,
  onRefresh,
}: {
  challenge: CaptchaChallenge | null
  value: string
  onChange: (value: string) => void
  onRefresh: () => void
}) {
  return (
    <div className="captcha-row">
      {challenge ? (
        // O SVG vem do servidor (texto distorcido + ruído) — confiamos nele
        // como imagem, o conteúdo é gerado por nós mesmos.
        <div className="captcha-canvas" dangerouslySetInnerHTML={{ __html: challenge.svg }} />
      ) : (
        <div className="captcha-canvas captcha-loading">carregando…</div>
      )}
      <button type="button" className="ghost-button tiny" onClick={onRefresh} title="Gerar outro código">
        ↻
      </button>
      <input
        className="captcha-input"
        value={value}
        maxLength={5}
        placeholder="Digite o código"
        onChange={(event) => onChange(event.target.value.toUpperCase())}
        required
      />
    </div>
  )
}
