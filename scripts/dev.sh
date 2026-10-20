#!/usr/bin/env bash
# Sobe o Loomia completo: compila (se necessário) e inicia o servidor.
#
#   ./scripts/dev.sh            # porta 8080, 2 bots
#   LOOMIA_PORT=9000 ./scripts/dev.sh
#
# O servidor serve o site em http://127.0.0.1:8080 (build do frontend) e prepara
# o cliente nativo em segundo plano, para que o botão LAUNCH funcione.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "==> Loomia: preparando workspace"
if ! command -v cargo >/dev/null 2>&1; then
  echo "ERRO: cargo (Rust) não encontrado. Instale em https://rustup.rs" >&2
  exit 1
fi

# Frontend: usa o build existente; se não existir, tenta compilar.
if [ ! -f loomia-frontend/dist/index.html ]; then
  echo "==> Frontend não compilado; rodando 'npm install && npm run build'"
  (cd loomia-frontend && npm install && npm run build)
fi

# Cliente + servidor em modo debug (deps otimizadas via profile.dev.package."*").
echo "==> Compilando cliente e servidor (primeira vez pode levar vários minutos)"
cargo build -p loomia-server -p loomia-client

echo "==> Iniciando servidor"
exec cargo run -p loomia-server
