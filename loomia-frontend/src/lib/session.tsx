// Sessão do jogador (contexto React).
//
// Guarda usuário + avatares; toda página lê daqui e o login/logout atualizam.

import { createContext, useCallback, useContext, useEffect, useMemo, useState } from 'react'
import { api, type Avatar, type User } from './api'

interface SessionValue {
  user: User | null
  avatars: Avatar[]
  loading: boolean
  activeAvatar: Avatar | null
  refresh: () => Promise<void>
  setSession: (user: User | null, avatars?: Avatar[]) => void
}

const SessionContext = createContext<SessionValue | null>(null)

export function SessionProvider({ children }: { children: React.ReactNode }) {
  const [user, setUser] = useState<User | null>(null)
  const [avatars, setAvatars] = useState<Avatar[]>([])
  const [loading, setLoading] = useState(true)

  const refresh = useCallback(async () => {
    try {
      const info = await api.session()
      setUser(info.user)
      setAvatars(info.avatars)
    } catch {
      setUser(null)
      setAvatars([])
    } finally {
      setLoading(false)
    }
  }, [])

  useEffect(() => {
    void refresh()
  }, [refresh])

  const setSession = useCallback((nextUser: User | null, nextAvatars: Avatar[] = []) => {
    setUser(nextUser)
    setAvatars(nextAvatars)
  }, [])

  const value = useMemo<SessionValue>(
    () => ({
      user,
      avatars,
      loading,
      activeAvatar: avatars.find((avatar) => avatar.isActive) ?? avatars[0] ?? null,
      refresh,
      setSession,
    }),
    [user, avatars, loading, refresh, setSession],
  )

  return <SessionContext.Provider value={value}>{children}</SessionContext.Provider>
}

export function useSession(): SessionValue {
  const value = useContext(SessionContext)
  if (!value) throw new Error('useSession precisa do SessionProvider')
  return value
}
