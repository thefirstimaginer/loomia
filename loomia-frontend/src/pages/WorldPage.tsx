// Página de um mundo: descrição, prévia 3D, criador (com avatar), visitas,
// estrelas — além de Jogar e Reportar.
import { useCallback, useEffect, useState } from 'react'
import { AvatarPreview } from '../components/AvatarPreview'
import { RequireLogin } from '../components/RequireLogin'
import { WorldPreview } from '../WorldPreview'
import { api, type MapDetail } from '../lib/api'
import { Link, useRoute } from '../lib/router'
import { useSession } from '../lib/session'

export function WorldPage() {
  const { user, loading } = useSession()
  const [route] = useRoute()
  const mapId = route.name === 'world' ? route.mapId : ''

  useEffect(() => {
    document.title = 'Mundo · Loomia'
  }, [mapId])

  if (loading) return <main className="page"><p className="muted">Carregando…</p></main>
  if (!user) return <RequireLogin intention="ver este mundo" />
  return <WorldDetail mapId={mapId} />
}

function WorldDetail({ mapId }: { mapId: string }) {
  const [detail, setDetail] = useState<MapDetail | null>(null)
  const [world, setWorld] = useState<import('../lib/api').WorldMap | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [launching, setLaunching] = useState(false)
  const [starring, setStarring] = useState(false)
  const [notice, setNotice] = useState<string | null>(null)
  const [reportOpen, setReportOpen] = useState(false)
  const [reportText, setReportText] = useState('')
  const [busy, setBusy] = useState(false)

  const load = useCallback(async () => {
    try {
      const [detail, world] = await Promise.all([api.mapDetail(mapId), api.map(mapId)])
      setDetail(detail)
      setWorld(world)
      setError(null)
    } catch (problem) {
      setError(problem instanceof Error ? problem.message : 'Mundo não encontrado')
    }
  }, [mapId])

  useEffect(() => {
    void load()
  }, [load])

  const play = async () => {
    setLaunching(true)
    setNotice(null)
    try {
      const result = await api.launch(mapId)
      setNotice(`${result.message}. Abrindo o cliente Loomia…`)
      window.location.href = result.launch_url
    } catch (problem) {
      setNotice(
        problem instanceof Error
          ? `${problem.message} — instale o cliente na página de Download se ainda não tiver.`
          : 'Não foi possível abrir a sala.',
      )
    } finally {
      setLaunching(false)
    }
  }

  const star = async () => {
    setStarring(true)
    try {
      const result = await api.starMap(mapId)
      setDetail((prev) => (prev ? { ...prev, stars: result.stars, starCount: prev.starCount + (result.starred ? 1 : -1) } : prev))
      setNotice(result.starred ? 'Você deu uma estrela a este mundo! ⭐' : 'Estrela removida.')
    } catch (problem) {
      setNotice(problem instanceof Error ? problem.message : 'Não foi possível avaliar.')
    } finally {
      setStarring(false)
    }
  }

  const report = async (event: React.FormEvent<HTMLFormElement>) => {
    event.preventDefault()
    setBusy(true)
    try {
      await api.report({ targetId: detail?.authorId ?? '', description: reportText.trim(), mapId })
      setNotice('Report enviado para a equipe. Obrigado!')
      setReportOpen(false)
      setReportText('')
    } catch (problem) {
      setNotice(problem instanceof Error ? problem.message : 'Não foi possível reportar.')
    } finally {
      setBusy(false)
    }
  }

  if (error && !detail) {
    return (
      <main className="page">
        <section className="gate">
          <p className="eyebrow">404</p>
          <h1>Mundo não encontrado</h1>
          <p className="lead">{error}</p>
          <Link to="/explore" className="launch-button">
            ⬅ VOLTAR PARA OS MUNDOS
          </Link>
        </section>
      </main>
    )
  }
  if (!detail) return <main className="page"><p className="muted">Carregando mundo…</p></main>

  const created = new Date(detail.createdAt).toLocaleDateString('pt-BR')

  return (
    <main className="page world-page">
      <section className="world-hero">
        <WorldPreview map={world} />
        <div className="world-hero-info">
          <p className="eyebrow">mundo da comunidade</p>
          <h1>{detail.name}</h1>
          <p className="lead">{detail.description || 'Sem descrição — entre e descubra!'}</p>

          <div className="world-stats">
            <span className="stat-chip">👁 {detail.visits} visita{detail.visits === 1 ? '' : 's'}</span>
            <button type="button" className="stat-chip star" onClick={() => void star()} disabled={starring}>
              ⭐ {detail.stars} estrela{detail.stars === 1 ? '' : 's'} · {detail.starCount} avaliações
            </button>
            <span className="stat-chip">🧱 {detail.blocks} blocos</span>
            <span className="stat-chip">👥 até {detail.maxPlayers} jogadores</span>
          </div>

          {detail.room_open && <p className="pill live">● {detail.online} jogadores online agora</p>}

          <div className="form-actions world-actions">
            <button className="launch-button" onClick={() => void play()} disabled={launching}>
              {launching ? 'ABRINDO SALA…' : '▶ JOGAR'}
            </button>
            <button className="ghost-button danger" onClick={() => setReportOpen((open) => !open)}>
              ⚠ Reportar mapa
            </button>
          </div>
          <Link to="/download" className="subtle-link">
            Ainda não tem o cliente? Baixe aqui
          </Link>
        </div>
      </section>

      <section className="card world-author">
        <div className="profile-identity">
          {detail.authorAvatar ? (
            <AvatarPreview look={detail.authorAvatar} size={96} />
          ) : (
            <div className="avatar-placeholder" style={{ width: 80, height: 100 }} />
          )}
          <div>
            <p className="eyebrow">criado por</p>
            <h2>
              {detail.authorId ? (
                <Link to={`/jogadores/${detail.authorId}`} className="link-button">
                  {detail.authorNickname ?? detail.author}
                </Link>
              ) : (
                detail.author
              )}
            </h2>
            <p className="muted">publicado em {created} · versão {detail.version}</p>
          </div>
        </div>
      </section>

      {notice && <p className="form-success">{notice}</p>}

      {reportOpen && (
        <section className="card">
          <div className="card-head">
            <h2>Reportar “{detail.name}”</h2>
            <button className="subtle-link" onClick={() => setReportOpen(false)}>
              fechar
            </button>
          </div>
          <p className="muted">Descreva o problema com este mapa. A equipe recebe o report com o seu nome.</p>
          <form onSubmit={report}>
            <label className="field">
              <span>Descrição</span>
              <textarea
                value={reportText}
                minLength={10}
                maxLength={2000}
                required
                placeholder="Ex.: o mapa tem conteúdo ofensivo no spawn."
                onChange={(event) => setReportText(event.target.value)}
                style={{ minHeight: 120 }}
              />
            </label>
            <div className="form-actions">
              <button className="launch-button small" type="submit" disabled={busy || reportText.trim().length < 10}>
                ENVIAR REPORT
              </button>
            </div>
          </form>
        </section>
      )}
    </main>
  )
}
