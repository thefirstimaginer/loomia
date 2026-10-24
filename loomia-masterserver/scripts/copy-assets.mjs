// Copia arquivos não-TS que o build precisa em tempo de execução
// (migrations SQL) para dentro de `dist/`.
import { cp, mkdir } from 'node:fs/promises'
import path from 'node:path'

const root = path.resolve(import.meta.dirname, '..')
const from = path.join(root, 'src', 'db', 'migrations')
const to = path.join(root, 'dist', 'src', 'db', 'migrations')
await mkdir(to, { recursive: true })
await cp(from, to, { recursive: true })
console.log('migrations copiadas para dist/')
