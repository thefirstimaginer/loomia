// Download: cliente Loomia (.AppImage) + instruções do protocolo loomia://.

import { useEffect, useState } from 'react'
import { api } from '../lib/api'
import { Link } from '../lib/router'

export function DownloadPage() {
  const [info, setInfo] = useState<{ registered: boolean; protocol: string; format: string } | null>(null)

  useEffect(() => {
    api
      .clientInfo()
      .then((result) => setInfo(result))
      .catch(() => setInfo(null))
  }, [])

  return (
    <main className="page download-page">
      <section className="page-head">
        <div>
          <p className="eyebrow">cliente oficial</p>
          <h1>Download do Loomia</h1>
          <p className="lead">
            O jogo é um cliente nativo (LoomiaEngine). Baixe uma vez, abra e depois entre pelos mundos direto no site.
          </p>
        </div>
      </section>

      <div className="download-grid">
        <section className="card download-card">
          <h2>Linux x86_64 · AppImage</h2>
          <p className="muted">Um arquivo só, sem instalação. É o formato recomendado.</p>
          <a className="launch-button" href="/api/client/download">
            ⬇ BAIXAR LOOMIA (.APPIMAGE)
          </a>
          <pre className="code-block">{`chmod +x Loomia-x86_64.AppImage
./Loomia-x86_64.AppImage`}</pre>
          <p className="muted small">
            No primeiro clique o cliente mostra “Cliente Loomia Pronto”, fica na bandeja do sistema e registra o link{' '}
            <code>loomia://</code> — uma única vez.
          </p>
        </section>

        <section className="card">
          <h2>Como entrar nos mundos</h2>
          <ol className="steps">
            <li>
              Abra o <strong>AppImage</strong> uma vez (ele fica na bandeja).
            </li>
            <li>
              Aqui no site, entre na sua conta e escolha um mundo em <Link to="explore">Explorar Mundos</Link>.
            </li>
            <li>
              Clique em <strong>Entrar no mundo</strong> — o navegador abre o link <code>loomia://</code> e o jogo
              conecta na sala.
            </li>
          </ol>
          <p className="muted small">
            Status do protocolo nesta máquina:{' '}
            {info ? (info.registered ? 'registrado ✓' : 'não registrado (abra o AppImage uma vez)') : 'verificando…'}
          </p>
        </section>

        <section className="card">
          <h2>Requisitos</h2>
          <ul className="plain-list">
            <li>Linux x86_64 com X11 (ou XWayland)</li>
            <li>~200 MB de espaço em disco</li>
            <li>Conta Loomia para entrar nos mundos e publicar mapas</li>
          </ul>
          <p className="muted small">
            Outros sistemas: o engine roda em Windows/macOS, mas o instalador oficial ainda é o AppImage Linux.
          </p>
        </section>
      </div>
    </main>
  )
}
