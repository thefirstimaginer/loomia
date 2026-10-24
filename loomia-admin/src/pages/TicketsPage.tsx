// Tickets: mensagens da comunidade (problemas com o cliente, conta, jogo,
// sugestões). O admin responde e fecha.

import { useCallback, useEffect, useState } from 'react'
import { adminApi, formatDate, type AdminTicket } from '../lib/api'

const STATUS = ['open', 'reviewing', 'resolved', 'closed'] as const
const CATEGORY: Record<string, string> = {
  client: 'cliente',
  account: 'conta',
  gameplay: 'jogo',
  suggestion: 'sugestão',
  other: 'outro',
}

export function TicketsPage() {
  const [status, setStatus] = useState<string>('open')
  const [tickets, setTickets] = useState<AdminTicket[]>([])
  const [drafts, setDrafts] = useState<Record<string, { status: string; resolution: string }>>({})
  const [message, setMessage] = useState<string | null>(null)

  const load = useCallback(async () => {
    try {
      setTickets((await adminApi.tickets(status)).tickets)
    } catch (problem) {
      setMessage(problem instanceof Error ? problem.message : 'Falha ao carregar tickets')
    }
  }, [status])

  useEffect(() => {
    void load()
  }, [load])

  const save = async (ticket: AdminTicket) => {
    const draft = drafts[ticket.id] ?? { status: ticket.status, resolution: ticket.resolution }
    try {
      await adminApi.updateTicket(ticket.id, draft.status, draft.resolution)
      setMessage(`Ticket #${ticket.id} atualizado.`)
      await load()
    } catch (problem) {
      setMessage(problem instanceof Error ? problem.message : 'Falha ao salvar')
    }
  }

  return (
    <div className="stack">
      <section className="panel toolbar">
        <select value={status} onChange={(event) => setStatus(event.target.value)} aria-label="Filtrar por status">
          <option value="">Todos</option>
          {STATUS.map((option) => (
            <option key={option} value={option}>
              {option}
            </option>
          ))}
        </select>
        <span className="muted small">{tickets.length} ticket(s)</span>
      </section>

      {message && <p className="notice ok">{message}</p>}

      <section className="panel">
        <ul className="report-list">
          {tickets.length === 0 && <li className="muted">Nenhum ticket nesse status.</li>}
          {tickets.map((ticket) => {
            const draft = drafts[ticket.id] ?? { status: ticket.status, resolution: ticket.resolution }
            return (
              <li key={ticket.id}>
                <header>
                  <div>
                    <span className="badge">{CATEGORY[ticket.category] ?? ticket.category}</span>
                    <strong> {ticket.subject}</strong>
                    <small className="muted block">
                      de {ticket.authorName} · {formatDate(ticket.createdAt)}
                    </small>
                  </div>
                  <span className={`badge status-${ticket.status}`}>{ticket.status}</span>
                </header>
                <p className="report-body">{ticket.body}</p>
                <div className="inline-form">
                  <select
                    value={draft.status}
                    onChange={(event) =>
                      setDrafts({ ...drafts, [ticket.id]: { ...draft, status: event.target.value } })
                    }
                    aria-label="Novo status"
                  >
                    {STATUS.map((option) => (
                      <option key={option} value={option}>
                        {option}
                      </option>
                    ))}
                  </select>
                  <input
                    placeholder="Resposta / resolução"
                    value={draft.resolution}
                    onChange={(event) =>
                      setDrafts({ ...drafts, [ticket.id]: { ...draft, resolution: event.target.value } })
                    }
                  />
                  <button className="button" onClick={() => void save(ticket)}>
                    Salvar
                  </button>
                </div>
              </li>
            )
          })}
        </ul>
      </section>
    </div>
  )
}
