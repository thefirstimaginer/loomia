// Catálogo: itens cosméticos da rede (chapéus e cores de avatar).
import { useEffect, useMemo, useState } from 'react'
import { api, type CatalogItem } from '../lib/api'
import { Link } from '../lib/router'
import { AvatarPreview } from '../components/AvatarPreview'

const RARITY: Record<CatalogItem['rarity'], { label: string; className: string }> = {
  common: { label: 'comum', className: 'common' },
  rare: { label: 'raro', className: 'rare' },
  epic: { label: 'épico', className: 'epic' },
  legendary: { label: 'lendário', className: 'legendary' },
}

const CATEGORIES = [
  { id: '', label: 'Tudo' },
  { id: 'hat', label: 'Chapéus' },
  { id: 'shirt', label: 'Camisas' },
  { id: 'color', label: 'Cores' },
] as const

/** Amostra visual do item: chapéu desenhado ou a cor real do cosmético. */
function ItemSwatch({ item }: { item: CatalogItem }) {
  const colorMatch = item.description.match(/#([0-9a-fA-F]{6})/)
  const hex = colorMatch ? `#${colorMatch[1]}` : null

  if (item.category === 'hat') {
    const hatId = item.id.replace('hat-', '')
    const look = { bodyColor: [234, 190, 25] as [number, number, number], shirtColor: [15, 120, 190] as [number, number, number], pantsColor: [35, 75, 45] as [number, number, number], hat: hatId as 'cap' | 'top_hat' | 'propeller' | 'crown' }
    return (
      <div className="catalog-stage">
        <AvatarPreview look={look} size={110} />
      </div>
    )
  }

  return (
    <div
      className="catalog-stage catalog-stage-color"
      style={hex ? { background: hex } : undefined}
    >
      {hex && <code>{hex}</code>}
    </div>
  )
}

export function CatalogPage() {
  const [search, setSearch] = useState('')
  const [category, setCategory] = useState<string>('')
  const [items, setItems] = useState<CatalogItem[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState(false)

  useEffect(() => {
    document.title = 'Catálogo · Loomia'
  }, [])

  useEffect(() => {
    const timer = setTimeout(() => {
      api
        .catalogItems(search)
        .then((result) => {
          setItems(result)
          setError(false)
        })
        .catch(() => {
          setItems([])
          setError(true)
        })
        .finally(() => setLoading(false))
    }, 250)
    return () => clearTimeout(timer)
  }, [search])

  const visible = useMemo(
    () => (category ? items.filter((item) => item.category === category) : items),
    [items, category],
  )

  return (
    <main className="page">
      <section className="page-head">
        <div>
          <p className="eyebrow">cosméticos da rede</p>
          <h1>Catálogo</h1>
          <p className="lead">Chapéus e cores para deixar o seu avatar com a sua cara. Compras chegam em breve!</p>
        </div>
        <div className="search-box">
          <input
            type="search"
            value={search}
            placeholder="Pesquisar no catálogo…"
            onChange={(event) => setSearch(event.target.value)}
            aria-label="Pesquisar no catálogo"
          />
          <span className="search-count">{loading ? '…' : `${visible.length} itens`}</span>
        </div>
      </section>

      <nav className="catalog-tabs" aria-label="Categorias">
        {CATEGORIES.map((entry) => (
          <button
            key={entry.id}
            type="button"
            className={category === entry.id ? 'hat-chip selected' : 'hat-chip'}
            onClick={() => setCategory(entry.id)}
          >
            {entry.label}
          </button>
        ))}
      </nav>

      {loading ? (
        <p className="muted">Carregando catálogo…</p>
      ) : visible.length === 0 ? (
        <div className="empty-state">
          <h3>{error ? 'Catálogo indisponível' : 'Nada encontrado'}</h3>
          <p className="muted">
            {error
              ? 'Não consegui falar com o servidor agora. Tente de novo em instantes.'
              : 'Tente outra busca ou mude a categoria.'}
          </p>
          <Link to="/explore" className="ghost-button">
            Explorar mundos
          </Link>
        </div>
      ) : (
        <ul className="catalog-grid">
          {visible.map((item) => (
            <li key={item.id} className="catalog-card">
              <ItemSwatch item={item} />
              <div className="catalog-info">
                <strong>{item.name}</strong>
                <p className="muted small">{item.description}</p>
              </div>
              <footer>
                <span className={`pill rarity-${RARITY[item.rarity].className}`}>{RARITY[item.rarity].label}</span>
                <span className="pill">{item.available ? `grátis · em breve 🪙` : 'em breve'}</span>
              </footer>
            </li>
          ))}
        </ul>
      )}
    </main>
  )
}
