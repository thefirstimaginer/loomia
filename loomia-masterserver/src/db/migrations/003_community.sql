-- Comunidade: mundo (visitas/estrelas), fórum (salas + posts) e blog.
--
--   • map_visits: um registro por sessão de jogo (incrementa no launch)
--   • map_stars:  uma estrela por jogador por mapa (toggle)
--   • forum_rooms / forum_posts: salas de discussão criadas por jogadores
--   • blog_posts: notícias oficiais da plataforma escritas pela equipe
--
-- Todos os textos têm limite de tamanho para o site não virar mural infinito.

-- ---------------------------------------------------------------------------
-- Mundo: visitas e estrelas
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS map_visits (
  id         BIGSERIAL PRIMARY KEY,
  map_id     TEXT        NOT NULL,
  user_id    BIGINT      REFERENCES users(id) ON DELETE SET NULL,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS map_visits_map_idx ON map_visits (map_id);

CREATE TABLE IF NOT EXISTS map_stars (
  map_id     TEXT        NOT NULL,
  user_id    BIGINT      NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  PRIMARY KEY (map_id, user_id)
);
CREATE INDEX IF NOT EXISTS map_stars_map_idx ON map_stars (map_id);

-- ---------------------------------------------------------------------------
-- Fórum: salas de discussão e posts
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS forum_rooms (
  id            BIGSERIAL PRIMARY KEY,
  author_id     BIGINT      REFERENCES users(id) ON DELETE SET NULL,
  author_name   TEXT        NOT NULL DEFAULT 'anônimo',
  title         TEXT        NOT NULL CHECK (char_length(title) BETWEEN 3 AND 120),
  description   TEXT        NOT NULL DEFAULT '' CHECK (char_length(description) <= 500),
  created_at    TIMESTAMPTZ NOT NULL DEFAULT now(),
  last_activity TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS forum_rooms_activity_idx ON forum_rooms (last_activity DESC);

CREATE TABLE IF NOT EXISTS forum_posts (
  id          BIGSERIAL PRIMARY KEY,
  room_id     BIGINT      NOT NULL REFERENCES forum_rooms(id) ON DELETE CASCADE,
  author_id   BIGINT      REFERENCES users(id) ON DELETE SET NULL,
  author_name TEXT        NOT NULL DEFAULT 'anônimo',
  body        TEXT        NOT NULL CHECK (char_length(body) BETWEEN 1 AND 2000),
  created_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS forum_posts_room_idx ON forum_posts (room_id, created_at ASC);

-- ---------------------------------------------------------------------------
-- Blog: notícias oficiais da plataforma
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS blog_posts (
  id         BIGSERIAL PRIMARY KEY,
  slug       TEXT        NOT NULL UNIQUE,
  author_id  BIGINT      REFERENCES users(id) ON DELETE SET NULL,
  author     TEXT        NOT NULL DEFAULT 'equipe loomia',
  title      TEXT        NOT NULL CHECK (char_length(title) BETWEEN 3 AND 160),
  tag        TEXT        NOT NULL DEFAULT 'notícia',
  summary    TEXT        NOT NULL DEFAULT '' CHECK (char_length(summary) <= 300),
  body       TEXT        NOT NULL DEFAULT '' CHECK (char_length(body) <= 20000),
  created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS blog_posts_created_idx ON blog_posts (created_at DESC);
