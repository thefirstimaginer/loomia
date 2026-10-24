// Cabeçalho + rodapé compartilhados por todas as páginas.

import { useSession } from '../lib/session'
import { Link, useRoute } from '../lib/router'

export function SiteHeader() {
  const { user, activeAvatar, loading } = useSession()
  const [route] = useRoute()

  return (
    <header className="topbar">
      <Link to="/" className="brand">
        <span className="brand-logo">LOOMIA</span>
        <span className="brand-tag">build · play · share</span>
      </Link>
      <nav className="topnav">
        <Link to="/explorar" className={route.name === 'explore' ? 'active' : ''}>
          Explorar Mundos
        </Link>
        <Link to="/corner" className={route.name === 'corner' ? 'active' : ''}>
          Loomier&apos;s Corner
        </Link>
        {user && (
          <Link to="/amigos" className={route.name === 'friends' ? 'active' : ''}>
            Amigos
          </Link>
        )}
        {user && (
          <Link to="/tickets" className={route.name === 'tickets' ? 'active' : ''}>
            Suporte
          </Link>
        )}
        <Link to="/download" className={route.name === 'download' ? 'active' : ''}>
          Download
        </Link>
      </nav>
      <div className="topbar-user">
        {loading ? (
          <span className="status-meta">carregando…</span>
        ) : user ? (
          <Link to="/perfil" className="profile-chip">
            <span className="profile-dot" />
            {user.nickname}
            {activeAvatar && <small>{activeAvatar.name}</small>}
          </Link>
        ) : (
          <>
            <Link to="/login" className="ghost-link">
              Entrar
            </Link>
            <Link to="/registrar" className="mini-cta">
              Criar conta
            </Link>
          </>
        )}
      </div>
    </header>
  )
}

export function SiteFooter() {
  return (
    <footer>
      <span>Loomia — inspirado no Roblox clássico de 2008</span>
      <span className="footer-links">
        <Link to="/explorar">Explorar Mundos</Link>
        <Link to="/corner">Loomier&apos;s Corner</Link>
        <Link to="/download">Download</Link>
      </span>
    </footer>
  )
}
