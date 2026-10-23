# Loomia

Jogo online de mundos em blocos, inspirado no Roblox clássico de 2008.

```text
[ COMPUTADOR DO JOGADOR ]                     [ SERVIDOR / NUVEM ]
┌─────────────────────────┐                   ┌─────────────────────────┐
│ Cliente Loomia          │ <------ UDP ----> │ Game Server (Go)        │
│ (AppImage: janela +     │    (por sala)     │ física autoritativa     │
│  bandeja + LoomiaEngine)│                   │ snapshots a 30 Hz       │
└─────────────────────────┘                   └─────────────────────────┘
             ▲                                            ▲
      (abre via loomia://)                      (aloca e reporta)
             │                                            │
┌─────────────────────────┐                   ┌─────────────────────────┐
│ Navegador Web           │ <----- HTTP ----> │ Master Server (Node.js) │
│ (site / lobby)          │                   │ contas · catálogo       │
└─────────────────────────┘                   └─────────────────────────┘
```

| Pasta                 | Linguagem            | Papel                                                      |
|-----------------------|----------------------|------------------------------------------------------------|
| `loomia-frontend`     | React + Vite         | site: contas, avatar, catálogo, botão “Jogar”              |
| `loomia-masterserver` | Node.js (nativo)     | API central, sessões, alocação de salas, links `loomia://` |
| `loomia-gameserver`   | Go                   | sala: física, colisão, snapshots UDP a 30 Hz               |
| `loomia-engine`       | C / Lua (fork LOVR)  | **LoomiaEngine**: engine do jogo + módulo `udp`            |
| `loomia-client-lovr`  | Lua + Python         | o jogo, o controlador e o build do **AppImage**            |
| `docs/protocolo.md`   | —                    | protocolo único (cliente ↔ sala) e constantes de física    |
| `data/`               | JSON                 | banco do master (`masterserver.json`) e mapas (`*.loom`)   |

## Como o “Jogar” funciona

1. O jogador abre o **AppImage** uma vez: aparece a janela “Cliente Loomia
   Pronto, vá para o site e se divirta!” e o ícone vai para a **bandeja**.
   Nesse primeiro clique o cliente registra `loomia://` — **uma única vez**.
2. No site ele escolhe um mapa e clica em **Jogar**.
3. O site chama `POST /api/launch`; o master aloca uma porta, sobe uma sala Go e
   responde com:

   ```text
   loomia://launch?server=127.0.0.1:7801&token=abc&map=baseplate&name=Ana&color=eabe19
   ```

4. O navegador abre o link; o sistema entrega ao cliente (a mesma instância já
   aberta), que baixa o mapa (`GET /api/maps/{id}`) e conecta na sala por UDP.
5. O cliente **prevê** o movimento com a mesma física do servidor e reconcilia
   com os snapshots.

O jogo **não abre porta própria**: o controlador mantém uma ponte UDP local
(`127.0.0.1:7788`) e o engine fala com ela — fechando o cliente, a conexão cai.

## Rodando em desenvolvimento

Pré-requisitos: Node.js 20+, Go 1.22+, Python 3 com Tk (só para a janela) e,
para o jogo, o LoomiaEngine (veja `loomia-engine/README.md`).

```bash
cd loomia-frontend && npm install && npm run build   # site
cd loomia-masterserver && npm start                  # master em :8080
loomia-client-lovr/appimage/build.sh                 # dist/Loomia-x86_64.AppImage
```

Para testar sem o engine: `./appimage/build.sh --no-engine` (janela e bandeja
funcionam; o jogo avisa que o engine não foi encontrado).

## Variáveis de ambiente (master)

| Variável                         | Padrão                                    | Descrição                              |
|----------------------------------|-------------------------------------------|----------------------------------------|
| `LOOMIA_PORT` / `LOOMIA_HOST`    | `8080` / `127.0.0.1`                      | HTTP do master/site                    |
| `LOOMIA_BIND`                    | `127.0.0.1`                                | interface de escuta do HTTP            |
| `LOOMIA_DATA_DIR`                | `../data`                                  | banco e mapas                          |
| `LOOMIA_GAMESERVER_BIN`          | `../loomia-gameserver/loomia-gameserver`   | binário da sala (sem ele usa `go run`) |
| `LOOMIA_GAMESERVER_PUBLIC_HOST`  | `127.0.0.1`                                | host que o cliente recebe no link      |
| `LOOMIA_GAMESERVER_PORT_MIN/MAX` | `7800` / `7899`                            | faixa de portas das salas              |
| `LOOMIA_ROOM_SECRET`             | `loomia-dev-secret`                        | autentica a sala nos endpoints internos|
| `LOOMIA_TCP`                     | desligado                                  | modo TCP da sala (só diagnóstico/testes)|
| `LOOMIA_CLIENT_BIN`              | `loomia-client`                            | lançador do cliente (scripts/docs)     |

## Protocolo

**Uma única fonte**: [`docs/protocolo.md`](docs/protocolo.md). As mensagens
canônicas e as constantes de física estão em
`loomia-gameserver/protocol_golden.json`; Go e Lua têm testes que leem esse
arquivo (`protocol_test.go`, `tests/protocol_spec.lua`) e a física é comparada
passo a passo (`physics_golden.json`, `tests/physics_spec.lua`).

## Testes

```bash
cd loomia-gameserver && go vet ./... && go test ./...
cd loomia-masterserver && node --test
lua loomia-client-lovr/tests/protocol_spec.lua
lua loomia-client-lovr/tests/physics_spec.lua
cd loomia-frontend && npm run build
```

## Limitações conhecidas

- **Build do LoomiaEngine**: o módulo `udp` compila; o link final precisa de um
  backend de GPU completo (recomenda-se Vulkan). Ver
  `loomia-engine/README.md#estado-do-build-neste-ambiente`.
- **Bandeja**: o helper usa XEmbed (GNOME com extensão de bandeja, KDE, XFCE,
  Cinnamon e MATE). Em desktops só-Wayland sem XWayland o ícone pode não
  aparecer; minimizar/fechar a janela continua mantendo o cliente vivo.
- **macOS/Windows**: o AppImage é Linux/x86_64. O jogo roda em outros sistemas
  pelo engine, mas o registro `loomia://` precisa de um `.app` (macOS) ou de um
  instalador (Windows).
