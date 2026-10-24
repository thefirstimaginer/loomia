import react, { reactCompilerPreset } from '@vitejs/plugin-react'
import babel from '@rolldown/plugin-babel'
import { defineConfig } from 'vite'

// O painel admin é um app separado do site do jogador.
//
//   dev:      http://127.0.0.1:5174 (o Vite faz proxy de /api para o master)
//   produção: servido pelo master em /admin (o build usa base '/admin/')
const LOOMIA_MASTER = process.env.LOOMIA_MASTER_URL ?? 'http://127.0.0.1:8080'
const ADMIN_BASE = process.env.LOOMIA_ADMIN_BASE ?? '/admin/'

export default defineConfig(({ command }) => ({
  base: command === 'build' ? ADMIN_BASE : '/',
  plugins: [react(), babel({ presets: [reactCompilerPreset()] })],
  server: {
    port: 5174,
    proxy: {
      '/api': { target: LOOMIA_MASTER, changeOrigin: true },
    },
  },
}))
