# Loomia

Jogo 3D online de mundos em blocos inspirado no **Roblox clássico de 2008** — cliente e
servidor escritos em **Rust**.

```
┌──────────────────────────────────────────────────────────────────────────┐
│ loomia-frontend (React + Vite)      site/lobby: status, mundos, avatar,   │
│                                     botão LAUNCH e prévia 3D do mapa     │
│            │ fetch /api, ws /ws                                          │
│            ▼                                                            │
│ loomia-server (Axum + Tokio)        API REST + WebSocket + estáticos    │
│   • simulação autoritativa a 30 Hz                                      │
│   • física (colisão AABB) e replicação                                  │
│   • salvamento de mundos (.loom) e registro de jogadores                │
│   • launcher: compila/inicia o cliente nativo                           │
│            ▲ ws://host:port/ws                                          │
│            │                                                            │
│ loomia-client (Rust + Bevy 3D)      janela nativa: "Carregando Loomia,   │
│   • mapa de blocos, avatares, chat                                      │
│   • previsão local + reconciliação                                      │
│            ▲                                                            │
│            └──── loomia-shared (protocolo, mundo e física comuns) ──────┘
└──────────────────────────────────────────────────────────────────────────┘
```

## Como rodar

Pré-requisitos: `cargo` (Rust) e, para recriar o site, `npm`.

### Opção 1 — tudo com o servidor (recomendado)

```bash
./scripts/dev.sh
```

Isso compila cliente e servidor, garante o build do frontend e sobe o servidor em
<http://127.0.0.1:8080>. Ele serve o **site**, a **API** e o **WebSocket**, e ainda
compila o cliente em segundo plano.

No site, clique em **LANÇAR LOOMIA**: o cliente abre em uma janela própria mostrando
"Carregando Loomia, Aguarde!" até o mundo ser recebido do servidor.

### Opção 2 — manual

```bash
# 1) (só na primeira vez, ou após mudar o site) build do frontend
cd loomia-frontend && npm install && npm run build && cd ..

# 2) servidor (serve o dist/, a API e o ws)
cargo run -p loomia-server

# 3) cliente (em outro terminal)
cargo run -p loomia-client -- --server ws://127.0.0.1:8080/ws --name SeuNome
```

Para desenvolver o site com hot-reload: `cd loomia-frontend && npm run dev` (porta 5173,
com proxy para a API em 8080).

### Segundo jogador na mesma máquina

```bash
./scripts/play.sh ws://127.0.0.1:8080/ws Amigo
```

## Controles

| Tecla / ação                          | Função                                     |
|---------------------------------------|--------------------------------------------|
| `W A S D` / setas                     | andar                                      |
| `Espaço`                              | pular                                      |
| botão direito (ou esquerdo) arrastar  | girar a câmera                             |
| roda do mouse                         | zoom                                       |
| `V`                                   | primeira/terceira pessoa                   |
| `T` ou `Enter`                        | abrir o chat (`Enter` envia, `Esc` cancela)|
| `R`                                   | respawn                                    |

## O mapa de teste

O servidor gera a **Baseplate Clássica** (homenagem ao estúdio de 2008): gramado de
80×80, rampa de tijolos em degraus, blocos coloridos, casa com porta e janela neon,
árvores, postes de luz e um spawn pad xadrez. O mapa é salvo em
`data/worlds/baseplate.loom` (JSON com blocos, cores e materiais) — é o equivalente
moderno de um `.rbxl`. Edite o arquivo e reinicie o servidor para ver as mudanças.

## Protocolo (resumo)

WebSocket em `/ws`, mensagens JSON com `type`:

* Cliente → servidor: `join`, `input` (30 Hz), `chat`, `respawn`, `ping`.
* Servidor → cliente: `hello`, `welcome` (id, mapa, jogadores, chat recente),
  `snapshot` (30 Hz, posições autoritativas), `playerJoined`/`playerLeft`, `chat`,
  `correction`, `respawned`, `pong`.

O cliente **prevê** o movimento rodando a mesma física do servidor
(`loomia-shared::physics`) e **reconcilia** quando o snapshot chega.

## API web

| Rota                     | Descrição                                             |
|--------------------------|-------------------------------------------------------|
| `GET /api/health`        | sonda de vida                                         |
| `GET /api/status`        | versão, uptime, jogadores, mapa, caminho do cliente   |
| `GET /api/worlds`        | catálogo de mundos                                    |
| `GET /api/worlds/{id}`   | mapa completo (blocos)                                |
| `POST /api/player`       | registra/atualiza avatar (`Appearance`)                |
| `GET /api/players`       | avatares registrados                                  |
| `POST /api/launch`       | inicia o cliente (só da máquina local por padrão)     |
| `GET /api/launch/status` | progresso da compilação do cliente                    |

## Configuração (variáveis de ambiente)

| Variável               | Padrão                        | Descrição                              |
|------------------------|-------------------------------|----------------------------------------|
| `LOOMIA_PORT` / `PORT` | `8080`                        | porta HTTP/WS                          |
| `LOOMIA_HOST`          | `127.0.0.1`                   | endereço exibido nas URLs              |
| `LOOMIA_BOTS`          | `2`                           | jogadores de demonstração na sala      |
| `LOOMIA_DATA_DIR`      | `./data`                      | mundos e `players.json`                |
| `LOOMIA_FRONTEND_DIR`  | `./loomia-frontend/dist`      | estáticos do site                      |
| `LOOMIA_CLIENT_COMMAND`| —                             | binário/comando alternativo do cliente |
| `LOOMIA_ALLOW_LAUNCH`  | —                             | `1` permite LAUNCH de fora da máquina  |
| `LOOMIA_LOG`           | `info`                        | `debug`, `warn`, `error`               |

## Estrutura

```
loomia-shared/   protocolo, mundo (.loom), física e avatar (compartilhados)
loomia-server/   Axum/Tokio: API, WebSocket, loop de tique, launcher
loomia-client/   Bevy: carregamento, mapa, avatares, câmera, HUD/chat, netcode
loomia-frontend/ React: lobby, avatar, botão LAUNCH, prévia do mundo
scripts/         dev.sh e play.sh
data/            gerado em runtime (mundos, jogadores, logs do cliente)
```

## Testes

```bash
cargo test                    # física e protocolo
cd loomia-frontend && npm run build
```
