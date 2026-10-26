import { useEffect, useRef } from 'react'
import type { Block, WorldMap } from './lib/api'

/**
 * Prévia 3D do mundo no site (canvas 2D com projeção isométrica).
 *
 * O jogo de verdade é o cliente nativo em LOVR (Lua); este canvas serve para
 * o visitante ver o mapa antes de lançar o jogo — é leve (sem WebGL / sem
 * three.js) e usa exatamente os mesmos dados de `/api/maps/:id`.
 */
export function WorldPreview({ map }: { map: WorldMap | null }) {
  const canvasRef = useRef<HTMLCanvasElement>(null)

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return

    let raf = 0
    let angle = 0.6
    const start = performance.now()

    const resize = () => {
      const dpr = Math.min(window.devicePixelRatio || 1, 2)
      const rect = canvas.getBoundingClientRect()
      canvas.width = Math.max(1, Math.floor(rect.width * dpr))
      canvas.height = Math.max(1, Math.floor(rect.height * dpr))
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
    }
    resize()
    const observer = new ResizeObserver(resize)
    observer.observe(canvas)

    const rgb = (c: [number, number, number], shade = 1) =>
      `rgb(${Math.min(255, c[0] * shade) | 0},${Math.min(255, c[1] * shade) | 0},${Math.min(255, c[2] * shade) | 0})`

    const draw = (now: number) => {
      const rect = canvas.getBoundingClientRect()
      const w = rect.width
      const h = rect.height
      const sky = map?.sky_color ?? [150, 200, 240]

      // Céu com gradiente.
      const gradient = ctx.createLinearGradient(0, 0, 0, h)
      gradient.addColorStop(0, rgb([sky[0] * 0.75, sky[1] * 0.85, sky[2]] as [number, number, number]))
      gradient.addColorStop(1, rgb([sky[0], sky[1] * 0.92, sky[2] * 0.8] as [number, number, number]))
      ctx.fillStyle = gradient
      ctx.fillRect(0, 0, w, h)

      // Sol.
      ctx.fillStyle = 'rgba(255, 245, 200, 0.9)'
      ctx.beginPath()
      ctx.arc(w * 0.82, h * 0.18, 26, 0, Math.PI * 2)
      ctx.fill()

      if (!map) {
        ctx.fillStyle = 'rgba(20, 30, 40, 0.55)'
        ctx.font = '600 15px system-ui, sans-serif'
        ctx.textAlign = 'center'
        ctx.fillText('carregando o mapa…', w / 2, h / 2)
        raf = requestAnimationFrame(draw)
        return
      }

      angle = 0.65 + Math.sin((now - start) / 6000) * 0.25
      const scale = Math.min(w, h) / 95
      const cos = Math.cos(angle)
      const sin = Math.sin(angle)
      const tilt = 0.55
      const cx = w / 2
      const cy = h * 0.62

      const project = (x: number, y: number, z: number): [number, number, number] => {
        const rx = x * cos - z * sin
        const rz = x * sin + z * cos
        return [cx + rx * scale, cy - y * scale + rz * scale * tilt, rz]
      }

      const solids = map.blocks.filter((block) => block.collidable !== false)
      const decorations = map.blocks.filter((block) => block.collidable === false)

      const drawBlock = (block: Block) => {
        const [bx, by, bz] = block.position
        const [sx, sy, sz] = block.size
        const hx = sx / 2
        const hz = sz / 2
        const y0 = by - sy / 2
        const y1 = by + sy / 2

        const corners = [
          [bx - hx, y0, bz - hz],
          [bx + hx, y0, bz - hz],
          [bx + hx, y0, bz + hz],
          [bx - hx, y0, bz + hz],
          [bx - hx, y1, bz - hz],
          [bx + hx, y1, bz - hz],
          [bx + hx, y1, bz + hz],
          [bx - hx, y1, bz + hz],
        ] as [number, number, number][]

        const projected = corners.map(([x, y, z]) => project(x, y, z))
        const normal = block.material === 'neon' ? 1.2 : 1

        // Faces do cubo. Antes elas eram pintadas numa ordem fixa e as faces
        // de trás ficavam POR CIMA das da frente — dava para "ver através" do
        // bloco. Agora cada face é pintada da mais distante para a mais
        // próxima (painter's algorithm), usando a profundidade média dos
        // vértices projetados.
        const faces: { idx: number[]; shade: number; depth?: number }[] = [
          { idx: [4, 5, 6, 7], shade: 1.05 * normal }, // topo
          { idx: [1, 2, 6, 5], shade: 0.72 * normal },
          { idx: [2, 3, 7, 6], shade: 0.86 * normal },
          { idx: [0, 3, 7, 4], shade: 0.6 * normal },
          { idx: [0, 1, 5, 4], shade: 0.66 * normal },
        ]
        for (const face of faces) {
          face.depth = face.idx.reduce((total, i) => total + projected[i][2], 0) / face.idx.length
        }
        faces.sort((a, b) => (a.depth ?? 0) - (b.depth ?? 0))

        for (const face of faces) {
          ctx.beginPath()
          face.idx.forEach((i, vertex) => {
            const [px, py] = projected[i]
            if (vertex === 0) ctx.moveTo(px, py)
            else ctx.lineTo(px, py)
          })
          ctx.closePath()
          ctx.fillStyle = rgb(block.color, face.shade)
          ctx.fill()
          ctx.strokeStyle = 'rgba(0,0,0,0.18)'
          ctx.lineWidth = 1
          ctx.stroke()
        }
      }

      const sorted = [...solids, ...decorations].sort((a, b) => {
        // Mesma métrica de profundidade das faces: maior rz = mais perto.
        const depthOf = (block: Block) => {
          const [bx, , bz] = block.position
          return bx * sin + bz * cos
        }
        return depthOf(a) - depthOf(b)
      })
      for (const block of sorted) drawBlock(block)

      raf = requestAnimationFrame(draw)
    }

    raf = requestAnimationFrame(draw)
    return () => {
      cancelAnimationFrame(raf)
      observer.disconnect()
    }
  }, [map])

  return (
    <div className="preview-frame">
      <canvas ref={canvasRef} className="preview-canvas" aria-label="Prévia do mundo Loomia" />
      <div className="preview-badge">{map ? `${map.blocks.length} blocos` : '—'}</div>
    </div>
  )
}
