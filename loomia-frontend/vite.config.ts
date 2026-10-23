import react, { reactCompilerPreset } from '@vitejs/plugin-react'
import babel from '@rolldown/plugin-babel'
import { defineConfig } from 'vite'

// A API e o WebSocket do lobby são servidos pelo master server (Node.js) em
// `loomia-masterserver` (porta 8080). Em desenvolvimento, o Vite faz proxy
// para ele — assim o site funciona tanto em `npm run dev` (5173) quanto
// servido diretamente pelo master.
const LOOMIA_MASTER = process.env.LOOMIA_MASTER_URL ?? 'http://127.0.0.1:8080'

// https://vite.dev/config/
export default defineConfig({
  plugins: [
    react(),
    babel({ presets: [reactCompilerPreset()] })
  ],
  server: {
    port: 5173,
    proxy: {
      '/api': { target: LOOMIA_MASTER, changeOrigin: true },
      '/lobby': { target: LOOMIA_MASTER.replace(/^http/, 'ws'), ws: true },
    },
  },
})
