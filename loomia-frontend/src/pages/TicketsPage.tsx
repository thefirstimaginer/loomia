// Suporte: tickets para problemas da plataforma, do cliente, do jogo ou
// sugestões. O jogador acompanha o status e a resposta da equipe.

import { useCallback, useEffect, useState } from 'react'
import { RequireLogin } from '../components/RequireLogin'
import { api, type Ticket } from '../lib/api'
import { useSession } from '../lib/session'

const CATEGORIES = [
  { id: 'client', label: 'Cliente / instalação' },
  { id: 'account', label: 'Minha conta' },
  { id: 'gameplay', label: 'Problema no jogo' },
  { id: 'suggestion', label: 'Sugestão' },
  { id: 'other', label: 'Outro assunto' },
]

const STATUS_LABEL: Record<Ticket['status'], string> = {
  open: 'aberto',
  reviewing: 'em análise',
  resolved: 'respondido',
  closed: 'fechado',
}

export function TicketsPage() {
  const { user, loading } = useSession()
  if (loading) return <main className="page"><p className="muted">Carregando…</p></main>
  if (!user) return <RequireLogin intention="abrir um chamado" />
  return <TicketsContent />
}

function TicketsContent() {
  const [tickets, setTickets] = useState<Ticket[]>([])
  const [category, setCategory] = useState('client')
  const [subject, setSubject] = useState('')
  const [body, setBody] = useState('')
  const [busy, setBusy] = useState(false)
  const [message, setMessage] = useState<{ kind: 'ok' | 'error'; text: string } | null>(null)

  const load = useCallback(async () => {
    try {
      setTickets((await api.myTickets()).tickets)
    } catch {
      setMessage({ kind: 'error', text: 'Não foi possível carregar seus chamados.' })
    }
  }, [])

  useEffect(() => {
    void load()
  }, [load])

  const submit = async (event: React.FormEvent) => {
    event.preventDefault()
    setBusy(true)
    setMessage(null)
    try {
      await api.createTicket({ category, subject: subject.trim(), body: body.trim() })
      setSubject('')
      setBody('')
      setMessage({ kind: 'ok', text: 'Chamado aberto! A equipe responde por aqui.' })
      await load()
    } catch (problem) {
      setMessage({ kind: 'error', text: problem instanceof Error ? problem.message : 'Falha ao enviar' })
    } finally {
      setBusy(false)
    }
  }

  return (
    <main className="page">
      <section className="page-head">
        <div>
          <p className="eyebrow">ajuda e sugestões</p>
          <h1>Suporte</h1>
          <p className="lead">
            Problemas com o cliente, com a conta, com um mundo — ou ideias para o Loomia. A equipe responde aqui.
          </p>
        </div>
      </section>

      <div className="split-columns">
        <section className="card">
          <h2>Abrir chamado</h2>
          <form className="auth-form" onSubmit={(event) => void submit(event)}>
            <label className="field">
              <span>Assunto do chamado</span>
              <select value={category} onChange={(event) => setCategory(event.target.value)}>
                {CATEGORIES.map((option) => (
                  <option key={option.id} value={option.id}>
                    {option.label}
                  </option>
                ))}
              </select>
            </label>
            <label className="field">
              <span>Título</span>
              <input
                value={subject}
                maxLength={120}
                placeholder="Resuma o que aconteceu"
                onChange={(event) => setSubject(event.target.value)}
                required
                minLength={3}
              />
            </label>
            <label className="field">
              <span>Descrição</span>
              <textarea
                value={body}
                maxLength={4000}
                placeholder="Conte com detalhes: o que tentou fazer, o que apareceu na tela..."
                onChange={(event) => setBody(event.target.value)}
                required
                minLength={10}
              />
            </label>
            {message && (
              <p className={message.kind === 'ok' ? 'form-success' : 'form-error'}>{message.text}</p>
            )}
            <button className="launch-button" type="submit" disabled={busy}>
              {busy ? 'ENVIANDO…' : 'ENVIAR CHAMADO'}
            </button>
          </form>
        </section>

        <section className="card">
          <h2>Meus chamados ({tickets.length})</h2>
          {tickets.length === 0 && <p className="muted">Nenhum chamado ainda.</p>}
          <ul className="ticket-list">
            {tickets.map((ticket) => (
              <li key={ticket.id}>
                <header>
                  <strong>{ticket.subject}</strong>
                  <span className={`pill ${ticket.status === 'resolved' ? 'live' : ''}`}>{STATUS_LABEL[ticket.status]}</span>
                </header>
                <p>{ticket.body}</p>
                {ticket.resolution && (
                  <div className="ticket-answer">
                    <span>Resposta da equipe</span>
                    <p>{ticket.resolution}</p>
                  </div>
                )}
                <small className="muted">{new Date(ticket.createdAt).toLocaleString('pt-BR')}</small>
              </li>
            ))}
          </ul>
        </section>
      </div>
    </main>
  )
}
