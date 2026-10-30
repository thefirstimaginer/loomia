import { describe, it, expect } from 'vitest'
import { render, screen } from '@testing-library/react'
import { Captcha } from './Captcha'

describe('Captcha', () => {
  it('renderiza o canvas do desafio quando há challenge', () => {
    render(
      <Captcha
        challenge={{ token: 'abc', svg: '<svg>código</svg>' }}
        value=""
        onChange={() => {}}
        onRefresh={() => {}}
      />,
    )
    expect(screen.getByText('código')).toBeDefined()
  })

  it('renderiza estado de carregamento sem challenge', () => {
    render(
      <Captcha
        challenge={null}
        value=""
        onChange={() => {}}
        onRefresh={() => {}}
      />,
    )
    expect(screen.getByText(/carregando/)).toBeDefined()
  })

  it('renderiza o botão de refresh', () => {
    render(
      <Captcha
        challenge={null}
        value=""
        onChange={() => {}}
        onRefresh={() => {}}
      />,
    )
    const buttons = screen.getAllByRole('button')
    expect(buttons.length).toBeGreaterThan(0)
    const refreshButton = buttons.find((b) => b.getAttribute('title') === 'Gerar outro código')
    expect(refreshButton).toBeDefined()
  })
})
