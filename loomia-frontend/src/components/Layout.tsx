// Cabeçalho + rodapé compartilhados por todas as páginas.

import { useSession } from '../lib/session'
import { Link, useRoute } from '../lib/router'
import alphalogo from "../assets/logoalpha.png";

export function SiteHeader() {
  const { user, activeAvatar, loading } = useSession()
  const [route] = useRoute()

  return (
    <header className="topbar">
      <Link to="/" className="brand">
        <img src={alphalogo} alt="The Loomia Logo!" />
        <span className="brand-tag">Em Construção!</span>
        {/*<span className="brand-tag">build · play · share</span>*/}
      </Link>
      <nav className="topnav">
        <Link to="/explore" className={route.name === 'explore' || route.name === 'world' ? 'active' : ''}>
          Mundos
        </Link>
        <Link to="/catalog" className={route.name === 'catalog' ? 'active' : ''}>
          Catálogo
        </Link>
        {user && (
          <Link to="/amigos" className={route.name === 'friends' ? 'active' : ''}>
            Amigos
          </Link>
        )}
        <Link to="/club" className={route.name === 'club' ? 'active' : ''}>
          Loomier&apos;s Club
        </Link>
        <Link to="/forum" className={route.name === 'forum' ? 'active' : ''}>
          Fórum
        </Link>
        <Link to="/blog" className={route.name === 'blog' ? 'active' : ''}>
          Blog
        </Link>
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
      <span>&copy; 2026 Loomia (Beta) — build · play · share</span>
      <span className="footer-links">
        <Link to="/download">Sobre a Loomia!</Link>
        <Link to="/download">Privacidade</Link>
        <Link to="/download">Termos</Link>
        <Link to="/download">Download</Link>
      </span>
    </footer>
  )
}
