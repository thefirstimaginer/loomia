// Jogadores: busca, ficha (nome, email, bio, criado em, mapas enviados) e as
// ações de moderação (excluir conta / banir / desbanir).

import { useCallback, useEffect, useState } from 'react'
import {
  adminApi,
  formatDate,
  relative,
  type AdminPlayer,
  type AdminReport,
  type AdminTicket,
} from '../lib/api'

interface Selection {
  player: AdminPlayer
  reports: AdminReport[]
  tickets: AdminTicket[]
}

export function PlayersPage() {
  const [term, setTerm] = useState('')
  const [status, setStatus] = useState('')
  const [players, setPlayers] = useState<AdminPlayer[]>([])
  const [selection, setSelection] = useState<Selection | null>(null)
  const [busy, setBusy] = useState(false)
  const [message, setMessage] = useState<{ kind: 'ok' | 'error'; text: string } | null>(null)
  const [confirm, setConfirm] = useState<'ban' | 'remove' | null>(null)
  const [reason, setReason] = useState('')

  const search = useCallback(async () => {
    try {
      const result = await adminApi.players(term, status)
      setPlayers(result.players)
    } catch (problem) {
      setMessage({ kind: 'error', text: problem instanceof Error ? problem.message : 'Falha na busca' })
    }
  }, [term, status])

  useEffect(() => {
    const timer = setTimeout(() => void search(), 250)
    return () => clearTimeout(timer)
  }, [search])

  const open = async (id: string) => {
    try {
      setSelection(await adminApi.player(id))
      setConfirm(null)
      setReason('')
      setMessage(null)
    } catch (problem) {
      setMessage({ kind: 'error', text: problem instanceof Error ? problem.message : 'Falha ao abrir' })
    }
  }

  const act = async (action: 'ban' | 'unban' | 'remove') => {
    if (!selection) return
    setBusy(true)
    setMessage(null)
    try {
      if (action === 'ban') await adminApi.banPlayer(selection.player.id, reason || 'violação das regras')
      if (action === 'unban') await adminApi.unbanPlayer(selection.player.id)
      if (action === 'remove') await adminApi.removePlayer(selection.player.id)

      setMessage({
        kind: 'ok',
        text:
          action === 'ban'
            ? 'Jogador banido: email bloqueado e apelido liberado.'
            : action === 'unban'
              ? 'Ban removido: o jogador pode entrar de novo.'
              : 'Conta excluída: email e apelido voltaram a ficar livres.',
      })
      setConfirm(null)
      await search()
      if (action === 'remove') setSelection(null)
      else await open(selection.player.id)
    } catch (problem) {
      setMessage({ kind: 'error', text: problem instanceof Error ? problem.message : 'Falha na ação' })
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="stack">
      <section className="panel toolbar">
        <input
          type="search"
          placeholder="Buscar por apelido, email ou nome…"
          value={term}
          onChange={(event) => setTerm(event.target.value)}
          aria-label="Buscar jogadores"
        />
        <select value={status} onChange={(event) => setStatus(event.target.value)} aria-label="Filtrar por status">
          <option value="">Todos os status</option>
          <option value="active">Ativos</option>
          <option value="banned">Banidos</option>
          <option value="removed">Removidos</option>
        </select>
        <span className="muted small">{players.length} resultado(s)</span>
      </section>

      {message && <p className={`notice ${message.kind}`}>{message.text}</p>}

      <div className="split">
        <section className="panel list-panel">
          <table>
            <thead>
              <tr>
                <th>Jogador</th>
                <th>Status</th>
                <th>Mapas</th>
                <th>Criado</th>
              </tr>
            </thead>
            <tbody>
              {players.map((player) => (
                <tr
                  key={player.id}
                  className={selection?.player.id === player.id ? 'selected' : ''}
                  onClick={() => void open(player.id)}
                >
                  <td>
                    <strong>{player.nickname ?? `${player.firstName} ${player.lastName}`}</strong>
                    <span className="muted small block">{player.email}</span>
                  </td>
                  <td>
                    <span className={`badge status-${player.status}`}>{player.status}</span>
                    {player.online && <span className="badge online">online</span>}
                    {player.bannedAt && <span className="muted small block">desde {formatDate(player.bannedAt)}</span>}
                  </td>
                  <td>{player.maps}</td>
                  <td className="muted small">{formatDate(player.createdAt)}</td>
                </tr>
              ))}
              {players.length === 0 && (
                <tr>
                  <td colSpan={4} className="muted">
                    Nenhum jogador encontrado.
                  </td>
                </tr>
              )}
            </tbody>
          </table>
        </section>

        <aside className="panel detail-panel">
          {!selection ? (
            <p className="muted">Selecione um jogador para ver a ficha.</p>
          ) : (
            <>
              <header className="panel-head">
                <div>
                  <h2>{selection.player.nickname ?? `${selection.player.firstName} ${selection.player.lastName}`}</h2>
                  <p className="muted small">
                    {selection.player.firstName} {selection.player.lastName} · conta desde{' '}
                    {formatDate(selection.player.createdAt)}
                  </p>
                </div>
                <span className={`badge status-${selection.player.status}`}>{selection.player.status}</span>
              </header>

              <dl className="facts">
                <div>
                  <dt>Email</dt>
                  <dd>{selection.player.email}</dd>
                </div>
                <div>
                  <dt>Bio</dt>
                  <dd>{selection.player.bio || <span className="muted">— sem bio —</span>}</dd>
                </div>
                <div>
                  <dt>Mapas enviados</dt>
                  <dd>{selection.player.maps}</dd>
                </div>
                <div>
                  <dt>Último acesso</dt>
                  <dd>{relative(selection.player.lastSeenAt)}</dd>
                </div>
                <div>
                  <dt>Reports contra</dt>
                  <dd>{selection.reports.length}</dd>
                </div>
                <div>
                  <dt>Tickets abertos</dt>
                  <dd>{selection.tickets.filter((ticket) => ticket.status !== 'resolved' && ticket.status !== 'closed').length}</dd>
                </div>
              </dl>

              {confirm && (
                <div className="confirm-box">
                  <p>
                    {confirm === 'ban'
                      ? 'Banir este jogador? O email fica bloqueado para sempre e o apelido é liberado.'
                      : 'Excluir a conta? Os dados somem, mas o jogador pode criar outra conta com o mesmo email.'}
                  </p>
                  {confirm === 'ban' && (
                    <input
                      placeholder="Motivo do ban (aparece no registro de bans)"
                      value={reason}
                      onChange={(event) => setReason(event.target.value)}
                    />
                  )}
                  <div className="actions">
                    <button
                      className={confirm === 'ban' ? 'button danger' : 'button'}
                      onClick={() => void act(confirm)}
                      disabled={busy}
                    >
                      {busy ? 'Enviando…' : confirm === 'ban' ? 'Confirmar ban' : 'Confirmar exclusão'}
                    </button>
                    <button className="button ghost" onClick={() => setConfirm(null)} disabled={busy}>
                      Cancelar
                    </button>
                  </div>
                </div>
              )}

              <div className="actions">
                {selection.player.status === 'banned' ? (
                  <button className="button" onClick={() => void act('unban')} disabled={busy}>
                    Desbanir
                  </button>
                ) : (
                  <button className="button danger" onClick={() => setConfirm('ban')} disabled={busy}>
                    Banir jogador
                  </button>
                )}
                <button className="button ghost danger-text" onClick={() => setConfirm('remove')} disabled={busy}>
                  Excluir conta
                </button>
              </div>

              {selection.reports.length > 0 && (
                <section className="sub-section">
                  <h3>Reports recebidos</h3>
                  <ul className="mini-list">
                    {selection.reports.map((report) => (
                      <li key={report.id}>
                        <span className={`badge status-${report.status}`}>{report.status}</span>
                        <p>{report.description}</p>
                        <small className="muted">
                          por {report.reporterName} · {formatDate(report.createdAt)}
                        </small>
                      </li>
                    ))}
                  </ul>
                </section>
              )}
            </>
          )}
        </aside>
      </div>
    </div>
  )
}
