import { describe, it, expect } from 'vitest'
import { rgbToHex, hexToRgb } from './api'

describe('rgbToHex', () => {
  it('converte preto', () => {
    expect(rgbToHex([0, 0, 0])).toBe('#000000')
  })
  it('converte branco', () => {
    expect(rgbToHex([255, 255, 255])).toBe('#ffffff')
  })
  it('converte cor amarela baseplate', () => {
    expect(rgbToHex([234, 190, 25])).toBe('#eabe19')
  })
  it('faz padronização com zero à esquerda', () => {
    expect(rgbToHex([0, 5, 10])).toBe('#00050a')
  })
})

describe('hexToRgb', () => {
  it('converte preto', () => {
    expect(hexToRgb('#000000')).toEqual([0, 0, 0])
  })
  it('converte branco', () => {
    expect(hexToRgb('#ffffff')).toEqual([255, 255, 255])
  })
  it('expande formato curto #fff', () => {
    expect(hexToRgb('#fff')).toEqual([255, 255, 255])
  })
  it('converte cor amarela baseplate', () => {
    expect(hexToRgb('#eabe19')).toEqual([234, 190, 25])
  })
})
