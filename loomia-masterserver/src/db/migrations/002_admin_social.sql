-- Painel de administração, moderação e recursos sociais.
--
--   • status da conta (active/offline/removed/banned), bio e último acesso
--   • bans com o email "queimado" (o email não pode registrar de novo)
--   • reports de jogadores e tickets de suporte
--   • amizades (pedido + mensagem de apresentação), favoritos e mensagens
--
-- Regras:
--   • apagar a conta REMOVE o usuário, mas libera email e apelido para cadastro.
--   • banir mantém o registro e BLOQUEIA o email para sempre; o apelido volta a
--     ficar livre (por isso o nickname vira NULL e o índice único é parcial).

ALTER TABLE users
  ADD COLUMN IF NOT EXISTS bio TEXT NOT NULL DEFAULT '',
  ADD COLUMN IF NOT EXISTS role TEXT NOT NULL DEFAULT 'player',
  ADD COLUMN IF NOT EXISTS status TEXT NOT NULL DEFAULT 'active',
  ADD COLUMN IF NOT EXISTS last_seen_at TIMESTAMPTZ,
  ADD COLUMN IF NOT EXISTS banned_at TIMESTAMPTZ,
  ADD COLUMN IF NOT EXISTS removed_at TIMESTAMPTZ;

ALTER TABLE users DROP CONSTRAINT IF EXISTS users_role_check;
ALTER TABLE users ADD CONSTRAINT users_role_check CHECK (role IN ('player', 'moderator', 'admin'));
ALTER TABLE users DROP CONSTRAINT IF EXISTS users_status_check;
ALTER TABLE users ADD CONSTRAINT users_status_check CHECK (status IN ('active', 'offline', 'removed', 'banned'));
ALTER TABLE users DROP CONSTRAINT IF EXISTS users_bio_len_check;
ALTER TABLE users ADD CONSTRAINT users_bio_len_check CHECK (char_length(bio) <= 500);

-- Apelido único só enquanto a conta pode usá-lo (banido libera o nome).
DROP INDEX IF EXISTS users_nickname_lower_idx;
CREATE UNIQUE INDEX IF NOT EXISTS users_nickname_lower_idx
  ON users (lower(nickname)) WHERE nickname IS NOT NULL;

-- O apelido passa a aceitar NULL (é o que acontece no ban).
ALTER TABLE users ALTER COLUMN nickname DROP NOT NULL;
ALTER TABLE users DROP CONSTRAINT IF EXISTS users_nickname_check;

CREATE INDEX IF NOT EXISTS users_status_idx ON users (status);
CREATE INDEX IF NOT EXISTS users_last_seen_idx ON users (last_seen_at DESC);
CREATE INDEX IF NOT EXISTS users_email_lower_idx_all ON users (lower(email));

-- ---------------------------------------------------------------------------
-- Banimentos: o email fica queimado mesmo que a conta seja excluída depois.
-- ---------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS bans (
  id            BIGSERIAL PRIMARY KEY,
  user_id       BIGINT      REFERENCES users(id) ON DELETE SET NULL,
  email_lower   TEXT        NOT NULL,
  nickname      TEXT,
  reason        TEXT        NOT NULL DEFAULT '',
  banned_by     BIGINT      REFERENCES users(id) ON DELETE SET NULL,
  banned_by_name TEXT       NOT NULL DEFAULT 'sistema',
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
  expires_at    TIMESTAMPTZ
);

CREATE INDEX IF NOT EXISTS bans_email_idx ON bans (lower(email_lower));
CREATE INDEX IF NOT EXISTS bans_created_idx ON bans (created_at DESC);

-- ---------------------------------------------------------------------------
-- Eventos de conta (auditoria e gráficos do painel)
-- ---------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS account_events (
  id          BIGSERIAL PRIMARY KEY,
  user_id     BIGINT      REFERENCES users(id) ON DELETE SET NULL,
  kind        TEXT        NOT NULL CHECK (kind IN ('registered', 'login', 'removed', 'banned', 'unbanned', 'restored')),
  actor_id    BIGINT      REFERENCES users(id) ON DELETE SET NULL,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS account_events_kind_idx ON account_events (kind, created_at DESC);

-- ---------------------------------------------------------------------------
-- Reports (jogador → jogador) e tickets (suporte/sugestão)
-- ---------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS reports (
  id            BIGSERIAL PRIMARY KEY,
  reporter_id   BIGINT      REFERENCES users(id) ON DELETE SET NULL,
  reporter_name TEXT        NOT NULL DEFAULT 'anônimo',
  target_id     BIGINT      REFERENCES users(id) ON DELETE SET NULL,
  target_name   TEXT        NOT NULL,
  description   TEXT        NOT NULL,
  status        TEXT        NOT NULL DEFAULT 'open'
                  CHECK (status IN ('open', 'reviewing', 'resolved', 'dismissed')),
  map_id        TEXT,
  resolution    TEXT        NOT NULL DEFAULT '',
  resolved_by   BIGINT      REFERENCES users(id) ON DELETE SET NULL,
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS reports_status_idx ON reports (status, created_at DESC);
CREATE INDEX IF NOT EXISTS reports_target_idx ON reports (target_id);

CREATE TABLE IF NOT EXISTS tickets (
  id            BIGSERIAL PRIMARY KEY,
  author_id     BIGINT      REFERENCES users(id) ON DELETE SET NULL,
  author_name   TEXT        NOT NULL DEFAULT 'anônimo',
  category      TEXT        NOT NULL DEFAULT 'other'
                  CHECK (category IN ('client', 'account', 'gameplay', 'suggestion', 'other')),
  subject       TEXT        NOT NULL,
  body          TEXT        NOT NULL,
  status        TEXT        NOT NULL DEFAULT 'open'
                  CHECK (status IN ('open', 'reviewing', 'resolved', 'closed')),
  resolution    TEXT        NOT NULL DEFAULT '',
  resolved_by   BIGINT      REFERENCES users(id) ON DELETE SET NULL,
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at    TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS tickets_status_idx ON tickets (status, created_at DESC);
CREATE INDEX IF NOT EXISTS tickets_author_idx ON tickets (author_id);

-- ---------------------------------------------------------------------------
-- Social: favoritos, pedidos de amizade (com mensagem) e mensagens
-- ---------------------------------------------------------------------------

CREATE TABLE IF NOT EXISTS favorites (
  user_id     BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  target_id   BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  PRIMARY KEY (user_id, target_id),
  CHECK (user_id <> target_id)
);

CREATE TABLE IF NOT EXISTS friend_requests (
  id          BIGSERIAL PRIMARY KEY,
  from_id     BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  to_id       BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  message     TEXT        NOT NULL DEFAULT '' CHECK (char_length(message) <= 500),
  status      TEXT        NOT NULL DEFAULT 'pending'
                CHECK (status IN ('pending', 'accepted', 'declined', 'cancelled')),
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  responded_at TIMESTAMPTZ,
  CHECK (from_id <> to_id)
);

-- Só um pedido pendente por par (em qualquer direção, verificado no app).
CREATE UNIQUE INDEX IF NOT EXISTS friend_requests_pending_idx
  ON friend_requests (least(from_id, to_id), greatest(from_id, to_id)) WHERE status = 'pending';
CREATE INDEX IF NOT EXISTS friend_requests_to_idx ON friend_requests (to_id, status);

CREATE TABLE IF NOT EXISTS messages (
  id          BIGSERIAL PRIMARY KEY,
  from_id     BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  to_id       BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  body        TEXT        NOT NULL CHECK (char_length(body) BETWEEN 1 AND 1000),
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now(),
  read_at     TIMESTAMPTZ,
  CHECK (from_id <> to_id)
);

CREATE INDEX IF NOT EXISTS messages_inbox_idx ON messages (to_id, created_at DESC);
CREATE INDEX IF NOT EXISTS messages_thread_idx ON messages (from_id, to_id, created_at DESC);
