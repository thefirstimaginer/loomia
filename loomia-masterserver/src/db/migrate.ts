// Aplica as migrations pendentes (usado por `npm run db:migrate` e no boot).

import { closeDatabase, migrate } from './index.js'

const invokedDirectly = process.argv[1]?.endsWith('migrate.ts') || process.argv[1]?.endsWith('migrate.js')
if (invokedDirectly) {
  migrate()
    .then((ran) => {
      console.log(ran.length > 0 ? `migrations aplicadas: ${ran.join(', ')}` : 'banco já está atualizado')
      return closeDatabase()
    })
    .then(() => process.exit(0))
    .catch((error) => {
      console.error('migrations falharam:', error)
      process.exit(1)
    })
}
