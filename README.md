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

| Pasta                 | Linguagem              | Papel                                                        |
|-----------------------|------------------------|--------------------------------------------------------------|
| `loomia-frontend`     | React + Vite           | site: landing, Explorar Mundos, contas, avatares, Corner, Download |
| `loomia-masterserver` | **TypeScript** + PostgreSQL | API, sessões (cookie + scrypt), avatares, mapas, salas, links `loomia://` |
| `loomia-gameserver`   | Go                     | sala: física, colisão, snapshots UDP a 30 Hz                 |
| `loomia-engine`       | C / Lua (fork LOVR)    | **LoomiaEngine**: engine do jogo + módulo `udp`              |
| `loomia-client-lovr`  | Lua + Python           | o jogo, o controlador e o build do **AppImage**              |
| `docs/protocolo.md`   | —                      | protocolo único (cliente ↔ sala) e constantes de física      |
| `data/`               | JSON                   | mapas (`worlds/*.loom`) — o catálogo vive no PostgreSQL      |

### Páginas do site
| Rota           | Página                                                                         |
|----------------|--------------------------------------------------------------------------------|
| `/`            | landing com prévia de mundo e convite para criar conta                         |
| `/explorar`    | **Explorar Mundos**: busca por nome/autor/descrição e entrar (**exige login**) |
| `/login`       | entrar com **email ou apelido** + senha                                        |
| `/registrar`   | Nome, Sobrenome, Apelido, Email, Senha, Repetir Senha                          |
| `/boas-vindas` | depois do registro: editor do **primeiro avatar** → segue para Explorar Mundos |
| `/perfil`      | conta + **vários avatares** (criar, editar, ativar, remover)                   |
| `/corner`      | **Loomier's Corner**: publicar/editar seus mundos (**exige login**)            |
| `/download`    | cliente Loomia (.AppImage) e instruções do `loomia://`                         |

**Regra de acesso:** entrar em um mundo e abrir a Loomier's Corner exigem
login; a landing e a lista pública servem de vitrine.

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
Pré-requisitos: **PostgreSQL 14+**, Node.js 20+, Go 1.22+, Python 3 com Tk (só
para a janela) e, para o jogo, o LoomiaEngine (`loomia-engine/README.md`).

```bash
# 1. banco (uma vez; pede a senha do sudo). Cria o usuário e o banco 'loomia'.
cd loomia-masterserver && npm run db:setup
#    ...ou, se preferir manualmente:
#    sudo -u postgres createuser --login --pwprompt loomia
#    sudo -u postgres createdb --owner=loomia loomia
# 2. backend: instala, compila e sobe (aplica migrations + seed no boot)
npm install && npm run build
npm start    # usa postgresql://loomia:loomia@127.0.0.1:5432/loomia por padrão
# 3. site (o master também serve o dist/)
cd ../loomia-frontend && npm install && npm run build
# 4. cliente (opcional)
../loomia-client-lovr/appimage/build.sh
```

**"password authentication failed for user \"loomia\"** significa que o
usuário/banco ainda não existem (ou a senha do `DATABASE_URL` é outra). Rode
`npm run db:setup` (ou o par de comandos acima) e use a **mesma** senha no
`DATABASE_URL`:

```bash
DATABASE_URL=postgresql://loomia:SUA_SENHA@127.0.0.1:5432/loomia npm start
```

Scripts do backend: `npm run db:setup`, `npm run dev` (tsx, recarrega),
`npm run db:migrate`, `npm run seed`, `npm test` (integração com PostgreSQL
real).

Para testar o cliente sem o engine: `./appimage/build.sh --no-engine` (janela
e bandeja funcionam; o jogo avisa que o engine não foi encontrado).

## Variáveis de ambiente (master)

| Variável                         | Padrão                                          | Descrição                              |
|----------------------------------|-------------------------------------------------|----------------------------------------|
| `DATABASE_URL`                   | `postgresql://loomia:loomia@127.0.0.1:5432/loomia` | conexão do PostgreSQL                  |
| `LOOMIA_DATABASE_SSL`            | desligado                                       | TLS no banco (produção)                |
| `LOOMIA_PORT` / `LOOMIA_HOST`    | `8080` / `127.0.0.1`                            | HTTP do master/site                    |
| `LOOMIA_BIND`                    | `127.0.0.1`                                     | interface de escuta do HTTP            |
| `LOOMIA_WORLDS_DIR`              | `data/worlds`                                   | arquivos `.loom`                       |
| `LOOMIA_FRONTEND_DIR`            | `loomia-frontend/dist`                          | site compilado                         |
| `LOOMIA_GAMESERVER_BIN`          | `../loomia-gameserver/loomia-gameserver`        | binário da sala (sem ele usa `go run`) |
| `LOOMIA_GAMESERVER_PUBLIC_HOST`  | `127.0.0.1`                                     | host que o cliente recebe no link      |
| `LOOMIA_GAMESERVER_PORT_MIN/MAX` | `7800` / `7899`                                 | faixa de portas das salas              |
| `LOOMIA_ROOM_SECRET`             | `loomia-dev-secret`                             | autentica a sala nos endpoints internos|
| `LOOMIA_TCP`                     | desligado                                       | modo TCP da sala (só diagnóstico/testes)|
| `LOOMIA_CLIENT_BIN`              | `loomia-client`                                 | lançador do cliente (scripts/docs)     |

## Segurança
* Senhas: **scrypt** (N=2^15, r=8, p=1) com salt aleatório e comparação em tempo
  constante (`src/auth/password.ts`). Nunca são devolvidas pela API.
* Sessões: token aleatório em cookie **HttpOnly** (SameSite=Lax); o banco guarda
  só o SHA-256 do token, com validade de 30 dias e logout que revoga.
* Login inválido responde sempre ``email/apelido ou senha inválidos`` e compara
  contra um hash falso quando a conta não existe (não vaza quais emails existem).
* Rate limit por IP em registro, login e lançamento de sala.
* 2FA: previsto para depois (a tabela de usuários já tem espaço para o segredo).

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
