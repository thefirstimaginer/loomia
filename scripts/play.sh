#!/usr/bin/env bash
# Inicia apenas o cliente, conectando a um servidor Loomia.
#
#   ./scripts/play.sh                       # ws://127.0.0.1:8080/ws
#   ./scripts/play.sh ws://192.168.0.10:8080/ws MeuNome
#   ./scripts/play.sh "" "" --autoplay      # modo demo (anda sozinho)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

SERVER="${1:-ws://127.0.0.1:8080/ws}"
NAME="${2:-Loomian$((RANDOM % 9000 + 1000))}"
shift 2 2>/dev/null || true

if [ -x target/debug/loomia-client ]; then
  exec target/debug/loomia-client --server "$SERVER" --name "$NAME" "$@"
else
  echo "==> Compilando o cliente..."
  cargo build -p loomia-client
  exec target/debug/loomia-client --server "$SERVER" --name "$NAME" "$@"
fi
