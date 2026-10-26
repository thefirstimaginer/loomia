// Catálogo de itens cosméticos (aba Catálogo do site).
//
// Por enquanto o catálogo serve os cosméticos que o cliente já sabe renderizar
// (chapéus e paletas de cores do avatar). Compra/posse chegam quando houver
// economia na rede — os itens aparecem como "em breve".

import type { RequestContext, Router } from './http.js'
import { sendJson } from './http.js'

const HATS: { id: string; label: string; rarity: string; price: number }[] = [
  { id: 'cap', label: 'Boné', rarity: 'common', price: 0 },
  { id: 'top_hat', label: 'Cartola', rarity: 'rare', price: 0 },
  { id: 'propeller', label: 'Hélice', rarity: 'epic', price: 0 },
  { id: 'crown', label: 'Coroa', rarity: 'legendary', price: 0 },
]

const BODY_COLORS = ['#eabe19', '#e0af8c', '#8c5a3c', '#f5d2aa', '#78c88c', '#bebec8']
const SHIRT_COLORS = ['#0f78be', '#be2828', '#e6e6e6', '#464646', '#f0a014', '#6e3ca0']
const PANTS_COLORS = ['#234b2d', '#1e1e28', '#5a4632', '#3c3c78', '#787878']

export function createCatalogApi(router: Router): void {
  router.get(/^\/api\/catalog$/, async (ctx) => {
    const search = (ctx.query.get('q') ?? '').trim().toLowerCase()
    const items = [
      ...HATS.map((hat) => ({
        id: `hat-${hat.id}`,
        name: hat.label,
        description: `Chapéu ${hat.label.toLowerCase()} para o seu avatar.`,
        category: 'hat' as const,
        rarity: hat.rarity as 'common' | 'rare' | 'epic' | 'legendary',
        price: hat.price,
        currency: 'coins' as const,
        available: true,
      })),
      ...SHIRT_COLORS.map((color, index) => ({
        id: `shirt-${index}`,
        name: `Cor de camisa ${index + 1}`,
        description: `Nova cor de camisa (${color}) para o editor de avatar.`,
        category: 'shirt' as const,
        rarity: 'common' as const,
        price: 0,
        currency: 'coins' as const,
        available: true,
      })),
      ...BODY_COLORS.map((color, index) => ({
        id: `body-${index}`,
        name: `Cor de pele ${index + 1}`,
        description: `Tom de corpo (${color}) para o editor de avatar.`,
        category: 'color' as const,
        rarity: 'common' as const,
        price: 0,
        currency: 'coins' as const,
        available: true,
      })),
      ...PANTS_COLORS.map((color, index) => ({
        id: `pants-${index}`,
        name: `Cor de calça ${index + 1}`,
        description: `Cor de calça (${color}) para o editor de avatar.`,
        category: 'color' as const,
        rarity: 'common' as const,
        price: 0,
        currency: 'coins' as const,
        available: true,
      })),
    ]
    const filtered = search
      ? items.filter((item) => item.name.toLowerCase().includes(search) || item.description.toLowerCase().includes(search))
      : items
    sendJson(ctx.res, 200, filtered)
  })
}
