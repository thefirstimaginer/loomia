import { SiteFooter, SiteHeader } from './components/Layout'
import { useRoute } from './lib/router'
import { SessionProvider } from './lib/session'
import { BlogPage } from './pages/BlogPage'
import { CatalogPage } from './pages/CatalogPage'
import { ClubPage } from './pages/ClubPage'
import { DownloadPage } from './pages/DownloadPage'
import { FriendsPage } from './pages/FriendsPage'
import { ExplorePage } from './pages/ExplorePage'
import { ForumPage } from './pages/ForumPage'
import { LandingPage } from './pages/LandingPage'
import { LoginPage } from './pages/LoginPage'
import { PlayerPage } from './pages/PlayerPage'
import { ProfilePage } from './pages/ProfilePage'
import { RegisterPage } from './pages/RegisterPage'
import { TicketsPage } from './pages/TicketsPage'
import { WelcomePage } from './pages/WelcomePage'
import { WorldPage } from './pages/WorldPage'

/**
 * O site do Loomia.
 *
 * Rotas:
 *   /                  landing
 *   /explore           aba de mundos (login obrigatório)
 *   /explore/:id       página de um mundo (preview, criador, visitas, estrelas)
 *   /catalog           catálogo de cosméticos
 *   /amigos            rede de jogadores + pedidos (login obrigatório)
 *   /forum             salas de discussão (login obrigatório)
 *   /blog              notícias oficiais da plataforma
 *   /club              Loomier's Club (criação de mundos, login obrigatório)
 *   /login /registrar  contas
 *   /boas-vindas       primeiro avatar (depois do registro)
 *   /perfil            conta + avatares
 *   /download          cliente Loomia (.AppImage)
 */
export default function App() {
  return (
    <SessionProvider>
      <Site />
    </SessionProvider>
  )
}

function Site() {
  const [route, navigate] = useRoute()
  return (
    <div className="loomia">
      <SiteHeader />
      {renderPage(route, navigate)}
      <SiteFooter />
    </div>
  )
}

type Route = ReturnType<typeof useRoute>[0]
type Navigate = (path: string, options?: { replace?: boolean }) => void

function renderPage(route: Route, navigate: Navigate) {
  switch (route.name) {
    case 'landing':
      return <LandingPage />
    case 'explore':
      return <ExplorePage />
    case 'world':
      return <WorldPage />
    case 'catalog':
      return <CatalogPage />
    case 'login':
      return <LoginPage onDone={(path) => navigate(path, { replace: true })} />
    case 'register':
      return <RegisterPage onDone={(path) => navigate(path, { replace: true })} />
    case 'welcome':
      return <WelcomePage onDone={(path) => navigate(path, { replace: true })} />
    case 'profile':
      return <ProfilePage onNavigate={navigate} />
    case 'club':
      return <ClubPage />
    case 'forum':
      return <ForumPage />
    case 'blog':
      return <BlogPage />
    case 'download':
      return <DownloadPage />
    case 'player':
      return <PlayerPage />
    case 'friends':
      return <FriendsPage />
    case 'tickets':
      return <TicketsPage />
    default:
      return (
        <main className="page">
          <section className="gate">
            <p className="eyebrow">404</p>
            <h1>Página não encontrada</h1>
            <p className="lead">Esse mundo (ou link) não existe. Volte para o começo.</p>
            <a className="launch-button" href="/">
              ⬅ IR PARA A PÁGINA INICIAL
            </a>
          </section>
        </main>
      )
  }
}
