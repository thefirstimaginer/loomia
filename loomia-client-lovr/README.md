# Cliente Loomia (Lua + AppImage)

O jogador abre o **AppImage**, vê a janela **“Cliente Loomia Pronto, vá para o
site e se divirta!”**, escolhe o mapa no site e o jogo entra na sala. A janela
pode ser minimizada: o cliente fica na **bandeja do sistema** até ser fechado
pelo menu do ícone ou pelo botão **Fechar Cliente**.

```text
site ──(link loomia://launch?server=…&token=…)──▶ AppImage (controlador Python)
                                                    │ registra loomia:// UMA vez
                                                    │ janela + bandeja (C/X11)
                                                    ▼
                                      LoomiaEngine (Lua) ──udp──▶ Sala (Go)
```

## Componentes

| Arquivo                        | O que faz                                                      |
|--------------------------------|----------------------------------------------------------------|
| `appimage/loomia-client.py`    | controlador: janela, bandeja, registro do protocolo, link, jogo |
| `appimage/AppRun`              | entrada do AppImage                                            |
| `appimage/tray/loomia-tray.c`  | ícone da bandeja em C/X11 (sem GTK/DBus)                       |
| `appimage/build.sh`            | monta `dist/Loomia-x86_64.AppImage`                            |
| `main.lua`, `src/`             | o jogo em Lua (roda no LoomiaEngine)                           |
| `src/net.lua`                  | rede pelo módulo nativo `udp` do engine                        |

## Registro do `loomia://` (uma única vez)

No **primeiro clique** o controlador cria:

* `~/.local/bin/loomia-client` — lançador que chama o AppImage atual;
* `~/.local/share/applications/loomia-client.desktop` — `Exec=…loomia-client %u`,
  `MimeType=x-scheme-handler/loomia`;
* o marcador `~/.local/state/loomia/protocol-registered`.

Depois disso nunca mais mexe no registro (`--register-only --force` refaz de
propósito). Se o AppImage for movido, só o lançador é regravado no próximo
clique — o link do site continua funcionando.

> O **master server não registra nada**: ele gera o link e consulta o registro
> apenas para diagnóstico (`GET /api/launch/info`).

## Janela e bandeja

* **Minimizar**: a janela some e o cliente continua rodando.
* **Fechar a janela** (`X`): também minimiza — nada é perdido.
* **Ícone → Fechar cliente** ou **Fechar Cliente** na janela: encerra jogo e
  cliente.
* Um segundo clique no AppImage (ou um novo link do site) traz a **mesma**
  instância para frente.

## Construir

```bash
loomia-client-lovr/appimage/build.sh              # engine + cliente
loomia-client-lovr/appimage/build.sh --no-engine  # só o controlador (teste)
```

Saída: `dist/Loomia-x86_64.AppImage`.

## Rede

* O jogo **não abre porta própria**: o controlador mantém uma ponte UDP local
  em `127.0.0.1:7788` e o engine fala com ela (`LOOMIA_BRIDGE_PORT`).
* O protocolo completo (mensagens, `ack`, física) está em
  [`docs/protocolo.md`](../docs/protocolo.md) e é validado por
  `loomia-gameserver/protocol_golden.json` com testes dos dois lados.

## Testes

```bash
lua loomia-client-lovr/tests/protocol_spec.lua   # protocolo × golden Go
lua loomia-client-lovr/tests/physics_spec.lua    # física × golden Go
```

Teclas do jogo: `WASD`, `Espaço`, botão direito (câmera), roda (zoom), `V`
(1ª/3ª pessoa), `T`/`Enter` (chat), `R` (respawn), `F1` (diagnóstico).
