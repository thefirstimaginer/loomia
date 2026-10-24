// Gráfico de rosca (donut) em SVG puro.
//
// Usado nos 4 cartões do dashboard. É acessível (role="img" + descrição) e não
// depende de biblioteca de gráficos.

export interface DonutSlice {
  label: string
  value: number
  color: string
}

function arcPath(cx: number, cy: number, radius: number, start: number, end: number): string {
  const point = (angle: number) => {
    const radians = ((angle - 90) * Math.PI) / 180
    return [cx + radius * Math.cos(radians), cy + radius * Math.sin(radians)]
  }
  const [x1, y1] = point(start)
  const [x2, y2] = point(end)
  const largeArc = end - start > 180 ? 1 : 0
  return `M ${x1} ${y1} A ${radius} ${radius} 0 ${largeArc} 1 ${x2} ${y2}`
}

export function Donut({
  slices,
  size = 150,
  thickness = 18,
  centerLabel,
  centerValue,
  description,
}: {
  slices: DonutSlice[]
  size?: number
  thickness?: number
  centerLabel?: string
  centerValue?: string | number
  description?: string
}) {
  const total = slices.reduce((sum, slice) => sum + Math.max(0, slice.value), 0)
  const radius = size / 2 - thickness / 2 - 2
  const cx = size / 2
  const cy = size / 2

  let angle = 0
  const segments = slices
    .filter((slice) => slice.value > 0)
    .map((slice) => {
      const sweep = (slice.value / (total || 1)) * 360
      const start = angle
      angle += sweep
      // Evita "fatias" de 360° (o SVG não desenha arco completo com 1 comando).
      const end = Math.min(angle, start + 359.999)
      return { ...slice, path: arcPath(cx, cy, radius, start, end), percent: (slice.value / (total || 1)) * 100 }
    })

  return (
    <figure className="donut">
      <svg
        width={size}
        height={size}
        viewBox={`0 0 ${size} ${size}`}
        role="img"
        aria-label={description ?? slices.map((slice) => `${slice.label}: ${slice.value}`).join(', ')}
      >
        <circle cx={cx} cy={cy} r={radius} fill="none" stroke="rgba(255,255,255,0.08)" strokeWidth={thickness} />
        {segments.map((segment) => (
          <path
            key={segment.label}
            d={segment.path}
            fill="none"
            stroke={segment.color}
            strokeWidth={thickness}
            strokeLinecap="butt"
          >
            <title>{`${segment.label}: ${segment.value} (${segment.percent.toFixed(1)}%)`}</title>
          </path>
        ))}
        {(centerValue !== undefined || centerLabel) && (
          <>
            <text x={cx} y={cy - 2} textAnchor="middle" className="donut-value">
              {centerValue ?? total}
            </text>
            <text x={cx} y={cy + 16} textAnchor="middle" className="donut-label">
              {centerLabel ?? 'total'}
            </text>
          </>
        )}
      </svg>
      <figcaption>
        {slices.map((slice) => (
          <span key={slice.label} className="legend-item">
            <i style={{ background: slice.color }} />
            {slice.label}
            <strong>{slice.value}</strong>
          </span>
        ))}
      </figcaption>
    </figure>
  )
}
