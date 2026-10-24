-- Cria o usuário e o banco que o Loomia usa em desenvolvimento.
--
-- Rode UMA vez, como o usuário administrador do PostgreSQL:
--
--   sudo -u postgres psql -f loomia-masterserver/setup-database.sql
--
-- (Se o seu servidor pede senha para o `postgres` via TCP:
--   psql -U postgres -h 127.0.0.1 -f setup-database.sql )
--
-- Ele é idempotente: rodar de novo não quebra nada.
--
-- Usuário: loomia   Senha: loomia   Banco: loomia
-- Em produção use uma senha forte e um DATABASE_URL próprio.

DO $$
BEGIN
  IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'loomia') THEN
    CREATE ROLE loomia LOGIN PASSWORD 'loomia';
  ELSE
    ALTER ROLE loomia WITH LOGIN PASSWORD 'loomia';
  END IF;
END
$$;

-- `CREATE DATABASE` não roda dentro de bloco/transação: usa `\gexec` para
-- só executar se o banco ainda não existir.
SELECT 'CREATE DATABASE loomia OWNER loomia'
WHERE NOT EXISTS (SELECT 1 FROM pg_database WHERE datname = 'loomia')\gexec

ALTER DATABASE loomia OWNER TO loomia;
GRANT ALL PRIVILEGES ON DATABASE loomia TO loomia;

-- No PostgreSQL 15+ ninguém cria tabela em `public` sem permissão explícita.
\connect loomia
ALTER SCHEMA public OWNER TO loomia;
GRANT ALL ON SCHEMA public TO loomia;

\echo 'Banco pronto: postgresql://loomia:loomia@127.0.0.1:5432/loomia'
