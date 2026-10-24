#!/usr/bin/env bash
# Prepara o PostgreSQL para o Loomia (cria usuário + banco) e confere a conexão.
#
#   ./loomia-masterserver/scripts/setup-db.sh [SENHA]
#
# Padrões: usuário `loomia`, banco `loomia`, senha `loomia`.
# O script pede `sudo` uma vez (para rodar `psql` como o usuário `postgres`).

set -euo pipefail

DB_USER="${LOOMIA_DB_USER:-loomia}"
DB_NAME="${LOOMIA_DB_NAME:-loomia}"
DB_PASS="${1:-${LOOMIA_DB_PASSWORD:-loomia}}"
DB_HOST="${LOOMIA_DB_HOST:-127.0.0.1}"
DB_PORT="${LOOMIA_DB_PORT:-5432}"

info() { printf '\033[1;36m[db]\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31m[db]\033[0m %s\n' "$*" >&2; exit 1; }

command -v psql >/dev/null || fail "psql não encontrado. Instale o PostgreSQL: sudo apt install postgresql"

info "criando/ajustando usuário '${DB_USER}' e banco '${DB_NAME}' (vai pedir a senha do sudo)"
sudo -u postgres psql -v ON_ERROR_STOP=1 \
  -v db_user="$DB_USER" -v db_pass="$DB_PASS" -v db_name="$DB_NAME" <<'SQL'
SELECT format('CREATE ROLE %I LOGIN PASSWORD %L', :'db_user', :'db_pass')
WHERE NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = :'db_user')\gexec
SELECT format('ALTER ROLE %I LOGIN PASSWORD %L', :'db_user', :'db_pass')\gexec
-- CREATEDB: os testes de integração criam um banco temporário próprio.
SELECT format('ALTER ROLE %I CREATEDB', :'db_user')\gexec
SELECT format('CREATE DATABASE %I OWNER %I', :'db_name', :'db_user')
WHERE NOT EXISTS (SELECT 1 FROM pg_database WHERE datname = :'db_name')\gexec
SQL

# Permissões no schema public (PostgreSQL 15+ exige isso para criar tabelas).
sudo -u postgres psql -v ON_ERROR_STOP=1 -d "$DB_NAME" \
  -c "ALTER SCHEMA public OWNER TO \"$DB_USER\"; GRANT ALL ON SCHEMA public TO \"$DB_USER\";" >/dev/null

# Extensões opcionais: só o superusuário pode instalar. O Loomia NÃO precisa de
# nenhuma (usa índices em lower()), mas criamos as úteis quando o pacote existe,
# para manter o banco pronto para o futuro.
sudo -u postgres psql -d "$DB_NAME" -v ON_ERROR_STOP=0 >/dev/null 2>&1 <<'SQL' || true
CREATE EXTENSION IF NOT EXISTS citext;
CREATE EXTENSION IF NOT EXISTS pgcrypto;
SQL

URL="postgresql://${DB_USER}:${DB_PASS}@${DB_HOST}:${DB_PORT}/${DB_NAME}"
info "testando a conexão com o banco..."
if PGPASSWORD="$DB_PASS" psql -h "$DB_HOST" -p "$DB_PORT" -U "$DB_USER" -d "$DB_NAME" -c 'select 1' >/dev/null 2>&1; then
  info "tudo certo. Inicie o master com:"
  printf '\n    DATABASE_URL=%s npm start\n' "$URL"
else
  fail "o usuário existe, mas a conexão TCP em ${DB_HOST}:${DB_PORT} falhou.
      - confira se o PostgreSQL está rodando: sudo systemctl status postgresql
      - confira o pg_hba.conf (precisa permitir 'host ... md5/scram-sha-256')
      - ou aponte o socket local: DATABASE_URL=postgresql://${DB_USER}:${DB_PASS}@/${DB_NAME}?host=/var/run/postgresql"
fi
