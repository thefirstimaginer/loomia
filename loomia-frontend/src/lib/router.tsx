// Roteador mínimo baseado em History API (sem dependências).
//
// Rotas: "/", "/explore", "/explore/:id", "/catalog", "/login", "/registrar",
//        "/boas-vindas", "/perfil", "/club", "/forum", "/blog", "/download",
//        "/amigos", "/tickets"

import { useEffect, useState } from 'react'

export type Route =
  | { name: 'landing' }
  | { name: 'explore' }
  | { name: 'catalog' }
  | { name: 'login' }
  | { name: 'register' }
  | { name: 'welcome' }
  | { name: 'profile' }
  | { name: 'club' }
  | { name: 'forum' }
  | { name: 'forumRoom'; roomId: string }
  | { name: 'blog' }
  | { name: 'download' }
  | { name: 'world'; mapId: string }
  | { name: 'play'; mapId: string }
  | { name: 'player'; playerId: string }
  | { name: 'friends' }
  | { name: 'tickets' }
  | { name: 'notFound' }

export function parseRoute(pathname: string): Route {
  const path = pathname.replace(/\/+$/, '') || '/'
  if (path === '/') return { name: 'landing' }
  if (path === '/explore' || path === '/mapas' || path === '/mundos') return { name: 'explore' }
  const world = path.match(/^\/world\/([\w-]+)(?:\/[\w-]*)?$/)
  if (world) return { name: 'world', mapId: world[1]! }
  if (path === '/login' || path === '/entrar') return { name: 'login' }
  if (path === '/registrar' || path === '/criar-conta') return { name: 'register' }
  if (path === '/boas-vindas' || path === '/onboarding') return { name: 'welcome' }
  if (path === '/perfil') return { name: 'profile' }
  if (path === '/club' || path === '/corner' || path === '/loomiers-corner') return { name: 'club' }
  if (path === '/forum') return { name: 'forum' }
  const room = path.match(/^\/forum\/([\w-]+)$/)
  if (room) return { name: 'forumRoom', roomId: room[1]! }
  if (path === '/blog') return { name: 'blog' }
  if (path === '/catalog' || path === '/catalogo') return { name: 'catalog' }
  if (path === '/download') return { name: 'download' }
  const play = path.match(/^\/jogar\/([\w-]+)$/)
  if (play) return { name: 'world', mapId: play[1]! }
  // Rota antiga /explore/:id também continua resolvendo.
  const legacyWorld = path.match(/^\/(?:explore|mundos)\/([\w-]+)$/)
  if (legacyWorld) return { name: 'world', mapId: legacyWorld[1]! }
  const player = path.match(/^\/jogadores\/(\d+)$/)
  if (player) return { name: 'player', playerId: player[1]! }
  if (path === '/amigos') return { name: 'friends' }
  if (path === '/tickets' || path === '/suporte') return { name: 'tickets' }
  return { name: 'notFound' }
}

/** URL amigável de um mundo: /world/<id>/<slug-do-nome>. */
export function worldPath(mapId: string, mapName = ''): string {
  const slug = mapName
    .normalize('NFD')
    .replace(/[\u0300-\u036f]/g, '')
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-+|-+$/g, '')
    .slice(0, 40)
  return slug ? `/world/${mapId}/${slug}` : `/world/${mapId}`
}

export const ROUTES_WITHOUT_HEADER: Route['name'][] = []

// Evento próprio de navegação. `popstate` só dispara ao usar o botão
// voltar/avançar do navegador; usar `PopStateEvent` sintético funcionava no
// papel, mas tinha ordem frágil entre listeners. Um evento dedicado deixa a
// navegação determinística.
const NAVIGATE_EVENT = 'loomia:navigate'

export function navigateTo(to: string, options: { replace?: boolean } = {}): void {
  if (options.replace) window.history.replaceState({}, '', to)
  else window.history.pushState({}, '', to)
  window.dispatchEvent(new CustomEvent(NAVIGATE_EVENT, { detail: to }))
  window.scrollTo({ top: 0 })
}

export function useRoute(): [Route, (to: string, options?: { replace?: boolean }) => void] {
  const [route, setRoute] = useState<Route>(() => parseRoute(window.location.pathname))

  useEffect(() => {
    const sync = () => setRoute(parseRoute(window.location.pathname))
    window.addEventListener('popstate', sync)
    window.addEventListener(NAVIGATE_EVENT, sync)
    return () => {
      window.removeEventListener('popstate', sync)
      window.removeEventListener(NAVIGATE_EVENT, sync)
    }
  }, [])

  return [route, navigateTo]
}

/** Âncora que navega pelo roteador (mantendo o botão do meio funcionando). */
export function Link({
  to,
  children,
  className,
  onClick,
}: {
  to: string
  children: React.ReactNode
  className?: string
  onClick?: () => void
}) {
  return (
    <a
      href={to}
      className={className}
      onClick={(event) => {
        if (event.metaKey || event.ctrlKey || event.shiftKey || event.button !== 0) return
        event.preventDefault()
        onClick?.()
        navigateTo(to)
      }}
    >
      {children}
    </a>
  )
}
