// Bloqueio de páginas que exigem conta (Explorar Mundos e Loomier's Corner).

import { Link } from '../lib/router'

export function RequireLogin({ intention }: { intention: string }) {
  return (
    <main className="page">
      <section className="gate">
        <p className="eyebrow">conta necessária</p>
        <h1>Entre para {intention}</h1>
        <p className="lead">
          Para entrar nos mundos e publicar na Loomier&apos;s Corner você precisa de uma conta Loomia. É rápido:
          email (ou apelido) e senha.
        </p>
        <div className="hero-actions">
          <Link to="/registrar" className="launch-button">
            ▶ CRIAR CONTA
          </Link>
          <Link to="/login" className="ghost-button">
            Já tenho conta
          </Link>
        </div>
      </section>
    </main>
  )
}
