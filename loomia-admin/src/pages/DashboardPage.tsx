// Dashboard: 4 cartões de rosca + últimos eventos.
//
//   1. jogadores online × offline
//   2. cadastros (últimos 24h × restante)
//   3. moderação (banidos × contas removidas × contas ativas)
//   4. mapas publicados (públicos × privados)

import { useEffect, useState } from 'react'
import { adminApi, type Dashboard } from '../lib/api'
import { Donut } from '../components/Donut'

export function DashboardPage() {
  const [data, setData] = useState<Dashboard | null>(null)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    let cancelled = false
    const load = () =>
      adminApi
        .dashboard()
        .then((next) => {
          if (!cancelled) setData(next)
        })
        .catch((problem) => {
          if (!cancelled) setError(problem instanceof Error ? problem.message : 'Falha ao carregar')
        })
    void load()
    const timer = setInterval(() => void load(), 15_000)
    return () => {
      cancelled = true
      clearInterval(timer)
    }
  }, [])

  if (error && !data) return <div className="panel error">{error}</div>
  if (!data) return <div className="panel">Carregando dashboard…</div>

  const registrationsRecent = Math.min(data.registrations.last24h, data.registrations.total)
  const registrationsOlder = Math.max(0, data.registrations.total - registrationsRecent)

  return (
    <div className="stack">
      <section className="kpi-row">
        <Kpi label="Jogadores online" value={data.users.online} hint={`janela de ${data.online_window_minutes} min`} />
        <Kpi label="Cadastros hoje" value={data.registrations.last24h} hint={`${data.registrations.last7d} nos últimos 7 dias`} />
        <Kpi label="Reports abertos" value={data.reports.open} hint={`${data.reports.reviewing} em análise`} />
        <Kpi label="Tickets abertos" value={data.tickets.open} hint={`${data.tickets.reviewing} em análise`} />
      </section>

      <section className="chart-grid">
        <article className="panel chart-card">
          <header>
            <h2>Jogadores</h2>
            <p>Ativos × offline ({data.users.total} contas)</p>
          </header>
          <Donut
            slices={[
              { label: 'Online', value: data.users.online, color: '#3ddc97' },
              { label: 'Offline', value: data.users.offline, color: '#5b6b82' },
            ]}
            centerValue={data.users.online}
            centerLabel="online"
          />
        </article>

        <article className="panel chart-card">
          <header>
            <h2>Cadastros</h2>
            <p>{data.registrations.total} usuários criados</p>
          </header>
          <Donut
            slices={[
              { label: 'Últimas 24h', value: registrationsRecent, color: '#4aa8ff' },
              { label: 'Mais antigos', value: registrationsOlder, color: '#2c3e5c' },
            ]}
            centerValue={data.registrations.total}
            centerLabel="contas"
          />
          <Sparkline points={data.registrations.series} color="#4aa8ff" />
        </article>

        <article className="panel chart-card">
          <header>
            <h2>Moderação</h2>
            <p>Banidos × removidos × ativos</p>
          </header>
          <Donut
            slices={[
              { label: 'Ativas', value: data.moderation.active, color: '#3ddc97' },
              { label: 'Banidas', value: data.moderation.banned, color: '#ff5c5c' },
              { label: 'Removidas', value: data.moderation.removed, color: '#f0a014' },
            ]}
            centerValue={data.moderation.banned + data.moderation.removed}
            centerLabel="punições"
          />
        </article>

        <article className="panel chart-card">
          <header>
            <h2>Mapas</h2>
            <p>{data.maps.byAuthor} criadores publicaram</p>
          </header>
          <Donut
            slices={[
              { label: 'Públicos', value: data.maps.public, color: '#b48cff' },
              { label: 'Privados', value: data.maps.private, color: '#3c3357' },
            ]}
            centerValue={data.maps.published}
            centerLabel="mapas"
          />
          <Sparkline points={data.maps.series} color="#b48cff" />
        </article>
      </section>

      <section className="panel">
        <header className="panel-head">
          <h2>Últimos eventos de conta</h2>
        </header>
        <ul className="event-list">
          {data.recent_events.length === 0 && <li className="muted">Nenhum evento ainda.</li>}
          {data.recent_events.map((event) => (
            <li key={event.id}>
              <span className={`badge event-${event.kind}`}>{event.kind}</span>
              <time>{new Date(event.createdAt).toLocaleString('pt-BR')}</time>
            </li>
          ))}
        </ul>
      </section>
    </div>
  )
}

function Kpi({ label, value, hint }: { label: string; value: number; hint: string }) {
  return (
    <article className="panel kpi">
      <span className="kpi-label">{label}</span>
      <strong className="kpi-value">{value}</strong>
      <small>{hint}</small>
    </article>
  )
}

/** Mini gráfico de linha (SVG) para a série diária. */
function Sparkline({ points, color }: { points: { date: string; count: number }[]; color: string }) {
  if (points.length < 2) return null
  const width = 220
  const height = 42
  const max = Math.max(1, ...points.map((point) => point.count))
  const step = width / (points.length - 1)
  const path = points
    .map((point, index) => `${index === 0 ? 'M' : 'L'} ${index * step} ${height - (point.count / max) * (height - 6) - 3}`)
    .join(' ')
  return (
    <svg className="sparkline" viewBox={`0 0 ${width} ${height}`} preserveAspectRatio="none" aria-hidden>
      <path d={path} fill="none" stroke={color} strokeWidth="2" />
    </svg>
  )
}
