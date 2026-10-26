// Loomier's Club: espaço do criador — lista, publica e atualiza seus mapas.

import { useCallback, useEffect, useState } from 'react'
import { RequireLogin } from '../components/RequireLogin'
import { api, type MapInfo, type WorldMap } from '../lib/api'
import { useSession } from '../lib/session'

const TEMPLATE: WorldMap = {
  id: '',
  name: '',
  spawn: [0, 1.5, 6],
  sky_color: [150, 200, 240],
  blocks: [
    { position: [0, -0.5, 0], size: [40, 1, 40], color: [100, 160, 60], material: 'grass' },
    { position: [0, 3, 0], size: [4, 2, 4], color: [190, 70, 60], material: 'brick' },
    { position: [8, 1, -6], size: [2, 6, 2], color: [230, 190, 40], material: 'neon', collidable: true },
  ],
}

const slugify = (name: string): string =>
  name
    .normalize('NFD')
    .replace(/[\u0300-\u036f]/g, '')
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 40)

export function ClubPage() {
  const { user, loading } = useSession()
  if (loading) return <main className="page"><p className="muted">Carregando…</p></main>
  if (!user) return <RequireLogin intention="abrir o Loomier's Club" />
  return <ClubContent />
}

function ClubContent() {
  const [maps, setMaps] = useState<MapInfo[]>([])
  const [loading, setLoading] = useState(true)
  const [editor, setEditor] = useState<{ id: string; name: string; description: string; visible: boolean; world: WorldMap } | null>(null)
  const [busy, setBusy] = useState(false)
  const [message, setMessage] = useState<{ kind: 'ok' | 'error'; text: string } | null>(null)
  const [importText, setImportText] = useState('')

  const load = useCallback(async () => {
    setLoading(true)
    try {
      setMaps((await api.myMaps()).maps)
    } catch {
      setMaps([])
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => {
    void load()
  }, [load])

  const startNew = () => {
    const suffix = Math.random().toString(36).slice(2, 6)
    setEditor({
      id: `meu-mundo-${suffix}`,
      name: 'Meu Mundo',
      description: '',
      visible: true,
      world: { ...TEMPLATE, id: `meu-mundo-${suffix}`, name: 'Meu Mundo' },
    })
    setImportText('')
    setMessage(null)
  }

  const startEdit = (map: MapInfo) => {
    void api
      .map(map.id)
      .then((world) => {
        setEditor({ id: map.id, name: map.name, description: map.description, visible: map.visibility === 'public', world })
        setMessage(null)
      })
      .catch(() => setMessage({ kind: 'error', text: 'Não consegui carregar o mapa para editar.' }))
  }

  const save = async (event: React.FormEvent) => {
    event.preventDefault()
    if (!editor) return
    setBusy(true)
    setMessage(null)
    try {
      const world: WorldMap = { ...editor.world, id: editor.id, name: editor.name, description: editor.description }
      if (maps.some((map) => map.id === editor.id)) {
        await api.updateMap(editor.id, { name: editor.name, description: editor.description, visible: editor.visible, world })
        setMessage({ kind: 'ok', text: 'Mapa atualizado.' })
      } else {
        await api.publishMap({ name: editor.name, description: editor.description, visible: editor.visible, world })
        setMessage({ kind: 'ok', text: 'Mapa publicado! Ele já aparece em Explorar Mundos.' })
      }
      setEditor(null)
      await load()
    } catch (problem) {
      setMessage({ kind: 'error', text: problem instanceof Error ? problem.message : 'Não foi possível salvar.' })
    } finally {
      setBusy(false)
    }
  }

  const importJson = () => {
    try {
      const parsed = JSON.parse(importText) as WorldMap
      if (!Array.isArray(parsed.blocks)) throw new Error('faltou "blocks"')
      const id = slugify(editor?.id || parsed.id || parsed.name || 'meu-mundo') || 'meu-mundo'
      setEditor({
        id,
        name: parsed.name || editor?.name || 'Meu Mundo',
        description: editor?.description ?? '',
        visible: editor?.visible ?? true,
        world: { ...parsed, id },
      })
      setMessage({ kind: 'ok', text: `Mapa importado: ${parsed.blocks.length} blocos.` })
    } catch (problem) {
      setMessage({ kind: 'error', text: `JSON inválido: ${problem instanceof Error ? problem.message : ''}` })
    }
  }

  return (
    <main className="page club-page">
      <section className="page-head">
        <div>
          <p className="eyebrow">para quem cria</p>
          <h1>Loomier&apos;s Club</h1>
          <p className="lead">Publique seus mundos, atualize os que já existem e veja quantas pessoas jogaram.</p>
        </div>
        <button type="button" className="launch-button" onClick={startNew}>
          + PUBLICAR MUNDO
        </button>
      </section>

      {message && <p className={`form-${message.kind === 'ok' ? 'success' : 'error'}`}>{message.text}</p>}

      <div className="club-layout">
        <section className="card">
          <h2>Meus mundos ({maps.length})</h2>
          {loading ? (
            <p className="muted">Carregando…</p>
          ) : maps.length === 0 ? (
            <div className="empty-state">
              <h3>Você ainda não publicou nada</h3>
              <p className="muted">Comece com o modelo pronto e adicione seus blocos no JSON.</p>
              <button type="button" className="ghost-button" onClick={startNew}>
                Criar meu primeiro mundo
              </button>
            </div>
          ) : (
            <ul className="club-list">
              {maps.map((map) => (
                <li key={map.id}>
                  <div>
                    <strong>{map.name}</strong>
                    <p className="muted small">
                      <code>{map.id}</code> · {map.blocks} blocos · {map.visibility === 'public' ? 'público' : 'privado'}
                    </p>
                  </div>
                  <button type="button" className="ghost-button tiny" onClick={() => startEdit(map)}>
                    Editar
                  </button>
                </li>
              ))}
            </ul>
          )}
        </section>

        <aside className="card club-help">
          <h2>Como publicar</h2>
          <ol className="steps">
            <li>Crie um mundo com o modelo pronto (ou cole um JSON de mapa).</li>
            <li>O <strong>id</strong> é a URL do mundo — só letras, números e hífen.</li>
            <li>Ao salvar, os blocos são gravados e o mapa aparece em Explorar Mundos.</li>
          </ol>
          <p className="muted small">
            Dica: comece pelo modelo e altere <code>blocks</code> no JSON. Cada bloco tem posição, tamanho, cor e
            material.
          </p>
        </aside>
      </div>

      {editor && (
        <section className="card editor-card">
          <div className="card-head">
            <h2>{maps.some((map) => map.id === editor.id) ? 'Editar mundo' : 'Novo mundo'}</h2>
            <button type="button" className="subtle-link" onClick={() => setEditor(null)}>
              Fechar
            </button>
          </div>

          <form onSubmit={(event) => void save(event)} className="map-form">
            <label className="field">
              <span>Nome do mundo</span>
              <input value={editor.name} onChange={(event) => setEditor({ ...editor, name: event.target.value })} required />
            </label>
            <label className="field">
              <span>Id (URL)</span>
              <input
                value={editor.id}
                onChange={(event) => setEditor({ ...editor, id: slugify(event.target.value) })}
                pattern="[a-z0-9](?:[a-z0-9\-]{0,38}[a-z0-9])?"
                title="2 a 40 caracteres: minúsculas, números e hífen"
                required
              />
            </label>
            <label className="field span-2">
              <span>Descrição</span>
              <input
                value={editor.description}
                maxLength={400}
                onChange={(event) => setEditor({ ...editor, description: event.target.value })}
              />
            </label>
            <label className="checkbox-row">
              <input
                type="checkbox"
                checked={editor.visible}
                onChange={(event) => setEditor({ ...editor, visible: event.target.checked })}
              />
              Público (aparece em Explorar Mundos)
            </label>

            <label className="field span-2">
              <span>Blocos do mundo (JSON)</span>
              <textarea
                value={JSON.stringify(editor.world, null, 2)}
                spellCheck={false}
                onChange={(event) => {
                  try {
                    const parsed = JSON.parse(event.target.value) as WorldMap
                    setEditor({ ...editor, world: parsed })
                  } catch {
                    // Enquanto o jogador digita, mantém o último JSON válido.
                  }
                }}
              />
            </label>

            <div className="form-actions span-2">
              <button type="submit" className="launch-button" disabled={busy}>
                {busy ? 'SALVANDO…' : 'SALVAR MUNDO'}
              </button>
              <button type="button" className="ghost-button" onClick={importJson} disabled={!importText.trim()}>
                Importar JSON colado
              </button>
            </div>
            <label className="field span-2">
              <span>Colar um mapa (.loom / JSON)</span>
              <textarea value={importText} spellCheck={false} onChange={(event) => setImportText(event.target.value)} />
            </label>
          </form>
        </section>
      )}
    </main>
  )
}
