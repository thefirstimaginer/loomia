// Prévia 2D do avatar (CSS puro) — usada no editor, no perfil e nos cards.
//
// As cores e o chapéu vêm do mesmo formato salvo no banco, então a prévia
// sempre reflete o que o cliente 3D vai mostrar.

import type { Avatar } from '../lib/api'
import { rgbToHex } from '../lib/api'

export interface AvatarLook {
  bodyColor: [number, number, number]
  shirtColor: [number, number, number]
  pantsColor: [number, number, number]
  hat: Avatar['hat']
}

export function AvatarPreview({ look, size = 96 }: { look: AvatarLook; size?: number }) {
  const body = rgbToHex(look.bodyColor)
  const shirt = rgbToHex(look.shirtColor)
  const pants = rgbToHex(look.pantsColor)
  const scale = size / 96
  return (
    // A caixa reserva a área já escalada; o avatar é escalado por dentro sem
    // afetar o layout (evita sobrepor o rótulo/nome abaixo).
    <div className="avatar-preview-frame" style={{ width: 96 * scale, height: 132 * scale }}>
      <div className="avatar-preview" style={{ transform: `scale(${scale})`, transformOrigin: 'top center' }}>
        <div className="av-head" style={{ background: body }}>
          {look.hat === 'cap' && <div className="av-cap" style={{ background: '#c82d2d' }} />}
          {look.hat === 'top_hat' && (
            <>
              <div className="av-tophat-brim" />
              <div className="av-tophat" />
            </>
          )}
          {look.hat === 'propeller' && (
            <>
              <div className="av-cap" style={{ background: '#fac828' }} />
              <div className="av-propeller" />
            </>
          )}
          {look.hat === 'crown' && <div className="av-crown" />}
          <div className="av-eyes">
            <span />
            <span />
          </div>
        </div>
        <div className="av-torso" style={{ background: shirt }} />
        <div className="av-legs" style={{ background: pants }} />
      </div>
    </div>
  )
}
