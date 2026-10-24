// Editor de avatar: nome, cores (pele/camisa/calça) e chapéu.
//
// O mesmo componente serve para criar (registro/boas-vindas e perfil) e para
// editar. Ele NÃO fala com a API — devolve o payload pronto pro pai salvar.

import { useState } from 'react'
import { AvatarPreview } from './AvatarPreview'
import type { Avatar, AvatarOptions, AvatarPayload } from '../lib/api'
import { hexToRgb, rgbToHex } from '../lib/api'

const FALLBACK_OPTIONS: AvatarOptions = {
  hats: [
    { id: 'cap', label: 'Boné' },
    { id: 'top_hat', label: 'Cartola' },
    { id: 'propeller', label: 'Hélice' },
    { id: 'crown', label: 'Coroa' },
  ],
  bodyColors: ['#eabe19', '#e0af8c', '#8c5a3c', '#f5d2aa', '#78c88c', '#bebec8'],
  shirtColors: ['#0f78be', '#be2828', '#e6e6e6', '#464646', '#f0a014', '#6e3ca0'],
  pantsColors: ['#234b2d', '#1e1e28', '#5a4632', '#3c3c78', '#787878'],
}

export interface AvatarEditorValue extends AvatarPayload {
  name: string
}

export function AvatarEditor({
  options = FALLBACK_OPTIONS,
  initial,
  defaultName = 'Meu avatar',
  submitLabel,
  busy = false,
  error,
  onSubmit,
  onCancel,
  compact = false,
}: {
  options?: AvatarOptions
  initial?: Avatar | null
  defaultName?: string
  submitLabel: string
  busy?: boolean
  error?: string | null
  onSubmit: (value: AvatarEditorValue) => void | Promise<void>
  onCancel?: () => void
  compact?: boolean
}) {
  const [name, setName] = useState(initial?.name ?? defaultName)
  const [body, setBody] = useState(initial ? rgbToHex(initial.bodyColor) : options.bodyColors[0]!)
  const [shirt, setShirt] = useState(initial ? rgbToHex(initial.shirtColor) : options.shirtColors[0]!)
  const [pants, setPants] = useState(initial ? rgbToHex(initial.pantsColor) : options.pantsColors[0]!)
  const [hat, setHat] = useState<Avatar['hat']>(initial?.hat ?? null)
  const [makeActive, setMakeActive] = useState(true)

  const look = {
    bodyColor: hexToRgb(body),
    shirtColor: hexToRgb(shirt),
    pantsColor: hexToRgb(pants),
    hat,
  }

  return (
    <form
      className={`avatar-editor-panel ${compact ? 'compact' : ''}`}
      onSubmit={(event) => {
        event.preventDefault()
        void onSubmit({ name: name.trim() || 'Meu avatar', ...look, isActive: makeActive })
      }}
    >
      <div className="avatar-stage">
        <AvatarPreview look={look} size={compact ? 96 : 132} />
        <label className="field compact">
          <span>Nome do avatar</span>
          <input
            value={name}
            maxLength={40}
            onChange={(event) => setName(event.target.value)}
            placeholder="Meu avatar"
          />
        </label>
      </div>

      <div className="avatar-options">
        <ColorRow label="Pele" colors={options.bodyColors} value={body} onChange={setBody} />
        <ColorRow label="Camisa" colors={options.shirtColors} value={shirt} onChange={setShirt} />
        <ColorRow label="Calça" colors={options.pantsColors} value={pants} onChange={setPants} />
        <div className="color-row">
          <span>Chapéu</span>
          <div className="swatches hat-swatches">
            <button
              type="button"
              className={`hat-chip ${hat === null ? 'selected' : ''}`}
              onClick={() => setHat(null)}
            >
              Nenhum
            </button>
            {options.hats.map((option) => (
              <button
                key={option.id}
                type="button"
                className={`hat-chip ${hat === option.id ? 'selected' : ''}`}
                onClick={() => setHat(option.id as Avatar['hat'])}
              >
                {option.label}
              </button>
            ))}
          </div>
        </div>

        <label className="checkbox-row">
          <input type="checkbox" checked={makeActive} onChange={(event) => setMakeActive(event.target.checked)} />
          Usar este avatar ao entrar nos mundos
        </label>
      </div>

      {error && <p className="form-error">{error}</p>}

      <div className="form-actions">
        <button className="launch-button" type="submit" disabled={busy}>
          {busy ? 'SALVANDO…' : submitLabel}
        </button>
        {onCancel && (
          <button type="button" className="ghost-button" onClick={onCancel} disabled={busy}>
            Cancelar
          </button>
        )}
      </div>
    </form>
  )
}

function ColorRow({
  label,
  colors,
  value,
  onChange,
}: {
  label: string
  colors: string[]
  value: string
  onChange: (color: string) => void
}) {
  return (
    <div className="color-row">
      <span>{label}</span>
      <div className="swatches">
        {colors.map((color) => (
          <button
            key={color}
            type="button"
            className={`swatch ${value.toLowerCase() === color.toLowerCase() ? 'selected' : ''}`}
            style={{ background: color }}
            aria-label={`${label} ${color}`}
            onClick={() => onChange(color)}
          />
        ))}
      </div>
    </div>
  )
}

export { FALLBACK_OPTIONS }
