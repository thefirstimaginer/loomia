// Reports: jogadores reportando outros jogadores (nome de quem reportou, quem
// foi reportado e a descrição). O admin muda o status e escreve a resolução.

import { useCallback, useEffect, useState } from 'react'
import { adminApi, formatDate, type AdminReport } from '../lib/api'

const STATUS = ['open', 'reviewing', 'resolved', 'dismissed'] as const

export function ReportsPage() {
  const [status, setStatus] = useState<string>('open')
  const [reports, setReports] = useState<AdminReport[]>([])
  const [drafts, setDrafts] = useState<Record<string, { status: string; resolution: string }>>({})
  const [message, setMessage] = useState<string | null>(null)

  const load = useCallback(async () => {
    try {
      setReports((await adminApi.reports(status)).reports)
    } catch (problem) {
      setMessage(problem instanceof Error ? problem.message : 'Falha ao carregar reports')
    }
  }, [status])

  useEffect(() => {
    void load()
  }, [load])

  const save = async (report: AdminReport) => {
    const draft = drafts[report.id] ?? { status: report.status, resolution: report.resolution }
    try {
      await adminApi.updateReport(report.id, draft.status, draft.resolution)
      setMessage(`Report #${report.id} atualizado.`)
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
        <span className="muted small">{reports.length} report(s)</span>
      </section>

      {message && <p className="notice ok">{message}</p>}

      <section className="panel">
        <ul className="report-list">
          {reports.length === 0 && <li className="muted">Nenhum report nesse status.</li>}
          {reports.map((report) => {
            const draft = drafts[report.id] ?? { status: report.status, resolution: report.resolution }
            return (
              <li key={report.id}>
                <header>
                  <div>
                    <strong>{report.targetName}</strong>
                    <span className="muted"> foi reportado por </span>
                    <strong>{report.reporterName}</strong>
                    <small className="muted block">{formatDate(report.createdAt)}</small>
                  </div>
                  <span className={`badge status-${report.status}`}>{report.status}</span>
                </header>
                <p className="report-body">{report.description}</p>
                <div className="inline-form">
                  <select
                    value={draft.status}
                    onChange={(event) =>
                      setDrafts({ ...drafts, [report.id]: { ...draft, status: event.target.value } })
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
                    placeholder="Resolução (fica registrado)"
                    value={draft.resolution}
                    onChange={(event) =>
                      setDrafts({ ...drafts, [report.id]: { ...draft, resolution: event.target.value } })
                    }
                  />
                  <button className="button" onClick={() => void save(report)}>
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
