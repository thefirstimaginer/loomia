import react, { reactCompilerPreset } from '@vitejs/plugin-react'
import babel from '@rolldown/plugin-babel'
import { defineConfig } from 'vite'

// A API e o WebSocket são servidos pelo `loomia-server` (porta 8080).
// Em desenvolvimento, o Vite faz proxy para ele — assim o site funciona tanto
// em `npm run dev` (5173) quanto servido diretamente pelo servidor.
const LOOMIA_SERVER = process.env.LOOMIA_SERVER_URL ?? 'http://127.0.0.1:8080'

// https://vite.dev/config/
export default defineConfig({
  plugins: [
    react(),
    babel({ presets: [reactCompilerPreset()] })
  ],
  server: {
    port: 5173,
    proxy: {
      '/api': { target: LOOMIA_SERVER, changeOrigin: true },
      '/ws': { target: LOOMIA_SERVER.replace(/^http/, 'ws'), ws: true },
    },
  },
})
