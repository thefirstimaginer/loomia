-- Schema inicial do Loomia (PostgreSQL 14+).
--
-- Contas: email e apelido são únicos e comparados SEM diferenciar maiúsculas
-- por índices em `lower(...)` — assim o app não precisa de superusuário nem da
-- extensão `citext` (que exige privilégio especial para ser criada).
--
-- A senha NUNCA é guardada em texto: `password_hash` é scrypt com salt
-- aleatório (ver src/auth/password.ts).

CREATE TABLE IF NOT EXISTS users (
  id            BIGSERIAL PRIMARY KEY,
  first_name    TEXT        NOT NULL CHECK (char_length(first_name) BETWEEN 1 AND 60),
  last_name     TEXT        NOT NULL CHECK (char_length(last_name) BETWEEN 1 AND 60),
  nickname      TEXT        NOT NULL CHECK (char_length(nickname) BETWEEN 3 AND 20),
  email         TEXT        NOT NULL,
  password_hash TEXT        NOT NULL,
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
  last_login_at TIMESTAMPTZ
);

-- Unicidade sem diferenciar maiúsculas (era o que o `citext` fazia).
CREATE UNIQUE INDEX IF NOT EXISTS users_email_lower_idx ON users (lower(email));
CREATE UNIQUE INDEX IF NOT EXISTS users_nickname_lower_idx ON users (lower(nickname));

-- Avatares: o jogador pode ter vários e escolher um como "ativo".
CREATE TABLE IF NOT EXISTS avatars (
  id           BIGSERIAL PRIMARY KEY,
  user_id      BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  name         TEXT        NOT NULL CHECK (char_length(name) BETWEEN 1 AND 40),
  body_color   SMALLINT[]  NOT NULL CHECK (array_length(body_color, 1) = 3),
  shirt_color  SMALLINT[]  NOT NULL CHECK (array_length(shirt_color, 1) = 3),
  pants_color  SMALLINT[]  NOT NULL CHECK (array_length(pants_color, 1) = 3),
  hat          TEXT        CHECK (hat IN ('cap', 'top_hat', 'propeller', 'crown')),
  is_active    BOOLEAN     NOT NULL DEFAULT false,
  created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at   TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS avatars_user_idx ON avatars (user_id);
-- No máximo um avatar ativo por conta.
CREATE UNIQUE INDEX IF NOT EXISTS avatars_one_active_idx ON avatars (user_id) WHERE is_active;

-- Maps da comunidade (arquivos .loom ficam em disco; aqui ficam os metadados).
CREATE TABLE IF NOT EXISTS maps (
  id          TEXT        PRIMARY KEY CHECK (id ~ '^[a-z0-9][a-z0-9-]{1,39}$'),
  name        TEXT        NOT NULL,
  description TEXT        NOT NULL DEFAULT '',
  author_id   BIGINT      REFERENCES users(id) ON DELETE SET NULL,
  author_name TEXT        NOT NULL DEFAULT 'comunidade',
  max_players INTEGER     NOT NULL DEFAULT 32 CHECK (max_players BETWEEN 1 AND 32),
  visible     BOOLEAN     NOT NULL DEFAULT true,
  version     TEXT        NOT NULL DEFAULT '',
  blocks      INTEGER     NOT NULL DEFAULT 0,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS maps_search_idx ON maps (visible, updated_at DESC);
CREATE INDEX IF NOT EXISTS maps_author_idx ON maps (author_id);

-- Sessões do site (token opaco; o hash vai ao banco).
CREATE TABLE IF NOT EXISTS sessions (
  token_hash  TEXT        PRIMARY KEY,
  user_id     BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  user_agent  TEXT        NOT NULL DEFAULT '',
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  expires_at  TIMESTAMPTZ NOT NULL,
  revoked_at  TIMESTAMPTZ
);

CREATE INDEX IF NOT EXISTS sessions_user_idx ON sessions (user_id);
