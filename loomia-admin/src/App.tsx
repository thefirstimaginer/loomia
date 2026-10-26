// Painel de administração do Loomia — mesmo visual do site do jogador.

import { useEffect, useState } from 'react'
import { adminApi, type AdminUser } from './lib/api'
import { DashboardPage } from './pages/DashboardPage'
import { LoginPage } from './pages/LoginPage'
import { PlayersPage } from './pages/PlayersPage'
import { RegisterPage } from './pages/RegisterPage'
import { ReportsPage } from './pages/ReportsPage'
import { TicketsPage } from './pages/TicketsPage'
import alphalogo from './assets/logoalpha.png'

type Tab = 'dashboard' | 'players' | 'reports' | 'tickets' | 'register'

const TABS: { id: Tab; label: string }[] = [
  { id: 'dashboard', label: 'Dashboard' },
  { id: 'players', label: 'Jogadores' },
  { id: 'reports', label: 'Reports' },
  { id: 'tickets', label: 'Tickets' },
  { id: 'register', label: 'Criar Admin' },
]

export default function App() {
  const [admin, setAdmin] = useState<AdminUser | null>(null)
  const [checking, setChecking] = useState(true)
  const [tab, setTab] = useState<Tab>('dashboard')

  useEffect(() => {
    adminApi
      .me()
      .then((result) => setAdmin(result.admin))
      .catch(() => setAdmin(null))
      .finally(() => setChecking(false))
  }, [])

  if (checking) return <main className="loading-screen">Verificando acesso…</main>
  if (!admin) return <LoginPage onLogin={setAdmin} />

  const logout = async () => {
    await adminApi.logout().catch(() => undefined)
    setAdmin(null)
  }

  return (
    <div className="admin">
      <header className="admin-bar">
        <div className="admin-brand">
          <img src={alphalogo} alt="The Loomia Logo!" className="admin-logo-img" />
          <span className="admin-tag">Em Construção!</span>
        </div>
        <nav className="admin-tabs">
          {TABS.map((entry) => (
            <button
              key={entry.id}
              className={tab === entry.id ? 'tab active' : 'tab'}
              onClick={() => setTab(entry.id)}
            >
              {entry.label}
            </button>
          ))}
        </nav>
        <div className="admin-user">
          <span className="profile-chip">
            <span className="profile-dot" />
            {admin.nickname ?? admin.firstName}
            <small>({admin.role})</small>
          </span>
          <button className="ghost-button tiny" onClick={() => void logout()}>
            Sair
          </button>
        </div>
      </header>

      <main className="admin-main">
        {tab === 'dashboard' && <DashboardPage />}
        {tab === 'players' && <PlayersPage />}
        {tab === 'reports' && <ReportsPage />}
        {tab === 'tickets' && <TicketsPage />}
        {tab === 'register' && <RegisterPage onCreated={setAdmin} />}
      </main>

      <footer className="admin-footer">
        <span>&copy; 2026 Loomia (Beta) — painel de administração</span>
      </footer>
    </div>
  )
}
