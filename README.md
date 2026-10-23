# Loomia

Jogo online de mundos em blocos, inspirado no Roblox clássico de 2008.

A arquitetura é dividida em quatro peças. O navegador só mostra o **site/lobby**;
o **jogo** é um cliente nativo (LOVR) que se conecta direto à **sala** do mapa.

```text
[ COMPUTADOR DO JOGADOR ]                  [ SERVIDOR / NUVEM ]
┌─────────────────────────┐               ┌─────────────────────────┐
│  Client (LOVR / Lua)    │ <--- UDP ---> │  Game Server (Go)       │
│  render 3D · previsão   │  (por sala)   │  física autoritativa    │
└─────────────────────────┘               └─────────────────────────┘
             ▲                                         ▲
      (abre via loomia://)                     (aloca e reporta)
             │                                         │
┌─────────────────────────┐               ┌─────────────────────────┐
│  Navegador Web          │ <-- HTTP -->  │  Master Server (Node)   │
│  (site / launcher)      │               │  contas · catálogo      │
└─────────────────────────┘               └─────────────────────────┘
```

| Pasta                  | Linguagem        | Papel                                                        |
|------------------------|------------------|--------------------------------------------------------------|
| `loomia-frontend`      | React + Vite     | site: contas, avatar, catálogo de mapas, botão “Jogar”        |
| `loomia-masterserver`  | Node.js (nativo) | API central, sessões, alocação de salas, handler `loomia://`  |
| `loomia-gameserver`    | Go               | sala dedicada: física, colisão, snapshots UDP a 30 Hz         |
| `loomia-client-lovr`   | Lua / LOVR       | cliente 3D: renderiza o mundo, HUD, chat, input               |
| `data/`                | JSON             | banco do master (`masterserver.json`) e mapas (`worlds/*.loom`) |

## Como o “Jogar” funciona

1. O jogador escolhe um mapa no site e clica em **Jogar**.
2. O site chama `POST /api/launch`. O master escolhe uma porta livre e sobe uma
   instância Go (`loomia-gameserver -map ... -port ... -token ...`).
3. A resposta traz o link customizado:

   ```text
   loomia://launch?server=127.0.0.1:7801&token=abc&map=baseplate&name=Ana&color=eabe19
   ```

4. O navegador abre esse link; o sistema operacional entrega ao **handler**
   (`data/launcher/loomia-launch.sh`, registrado como `x-scheme-handler/loomia`),
   que converte a URL em argumentos do LOVR:

   ```bash
   lovr loomia-client-lovr server=127.0.0.1:7801 token=abc map=baseplate name=Ana
   ```

5. O cliente baixa o mapa em `GET /api/maps/{id}` (HTTP, no master) e conecta
   na sala por **UDP**, com o token no `hello`.
6. A sala valida o token, coloca o jogador no spawn e começa a mandar snapshots.
   O cliente **prevê** o movimento localmente e reconcilia com o servidor.

> O UDP só carrega mensagens pequenas (input, snapshots, chat). O mapa inteiro
> vai por HTTP no master — por isso a sala não precisa ter o `.loom` em disco
> além do que carregou na inicialização.

## Rodando em desenvolvimento

Pré-requisitos: Node.js 20+, Go 1.22+, LOVR 0.18+ e Lua 5.4 (com `luasocket`,
usado pela ponte de rede do cliente). O site precisa de `npm install` uma vez.

```bash
# 1. site (opcional em dev: o master também serve o dist/)
cd loomia-frontend && npm install && npm run dev

# 2. master server (porta 8080) — sobe o cliente do site e registra loomia://
cd loomia-masterserver && npm start

# 3. (build do site para o master servir)
cd loomia-frontend && npm run build

# 4. cliente, manualmente (sem link):
lovr loomia-client-lovr server=127.0.0.1:7801 token=SEU_TOKEN name=Ana
```

Abra <http://127.0.0.1:5173> (Vite) ou <http://127.0.0.1:8080> (site servido
pelo master) e clique em **Jogar**.

### Variáveis de ambiente (master)

| Variável                        | Padrão                         | Descrição                                  |
|---------------------------------|--------------------------------|--------------------------------------------|
| `LOOMIA_PORT`                   | `8080`                         | porta HTTP do master/site                  |
| `LOOMIA_BIND`                   | `127.0.0.1`                    | interface de escuta do HTTP                |
| `LOOMIA_DATA_DIR`               | `../data`                      | banco e mapas                             |
| `LOOMIA_GAMESERVER_BIN`         | `../loomia-gameserver/loomia-gameserver` | binário da sala (sem ele usa `go run`) |
| `LOOMIA_GO`                     | `go`                           | interpretador Go para o fallback          |
| `LOOMIA_GAMESERVER_BIND`        | `127.0.0.1`                    | interface UDP da sala                     |
| `LOOMIA_GAMESERVER_PUBLIC_HOST` | `127.0.0.1`                    | host que o cliente recebe no link         |
| `LOOMIA_GAMESERVER_PORT_MIN/MAX`| `7800` / `7899`                | faixa de portas das salas                 |
| `LOOMIA_ROOM_SECRET`            | `loomia-dev-secret`            | autentica a sala nos endpoints internos   |
| `LOOMIA_PROTOCOL_HANDLER`       | `auto`                         | `auto` registra `loomia://` no SO; `none` só gera o script |
| `LOVR`                          | `/usr/bin/lovr`                | executável do LOVR usado pelo handler     |
| `LOOMIA_LOVR_CLIENT_DIR`        | `../loomia-client-lovr`        | projeto Lua passado ao LOVR               |

## Protocolo da sala (UDP, JSON)

Cliente → sala:

```json
{"t":"hello","token":"...","name":"Ana","appearance":{...}}
{"t":"input","seq":12,"move_x":1,"move_z":0,"jump":false,"yaw":0.4}
{"t":"chat","text":"oi"}
{"t":"ping","id":7,"time":123.4}
{"t":"respawn"}
```

Sala → cliente:

```json
{"t":"welcome","id":3,"tick":120,"spawn":[0,1,4],"you":{...},"players":[...],"chat":[...]}
{"t":"snapshot","tick":121,"ack":12,"players":[{"id":4,"p":[...],"v":[...],"yaw":0,"ground":true}]}
{"t":"players","players":[{"id":4,"name":"Bob"}]}
{"t":"chat","from":"Bob","text":"oi"}
{"t":"pong","id":7,"time":123.4}
{"t":"respawn","p":[...]}
```

O snapshot **não** traz a posição do próprio jogador: o cliente confia na sua
previsão e usa o `ack` (último `seq` processado) para descartar inputs antigos.

## Testes

```bash
# sala Go: vet + build
cd loomia-gameserver && go vet ./... && go build ./...

# master: unitários (contas, mapas, parsing do link) + integração com a sala Go
cd loomia-masterserver && npm test
# a integração é pulada se o binário da sala não existir:
LOOMIA_GAMESERVER_BIN=../loomia-gameserver/loomia-gameserver npm test

# site
cd loomia-frontend && npm run build
```

## Pendências conhecidas

- **Transporte UDP no cliente LOVR.** O LOVR não expõe UDP na API Lua. O
  `src/net.lua` fala com um processo auxiliar (`src/netbridge.lua`, requer
  LuaSocket) por linhas JSON; falta uma ponte nativa (módulo C ou integração no
  `lovr` customizado) para o jogo rodar sem processo separado. Enquanto isso,
  abrir o cliente sem essa ponte mostra “rede UDP indisponível” no HUD.
- **macOS:** o registro de `loomia://` precisa de um `.app` com
  `CFBundleURLTypes` (no Linux/Windows o master registra automaticamente).
- **Hospedagem:** para produção, o master deve rodar com
  `LOOMIA_PROTOCOL_HANDLER=none` (não mexe no SO do servidor) e o handler é
  distribuído no instalador do cliente.
