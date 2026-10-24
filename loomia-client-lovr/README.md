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

## Interface do cliente (launcher)

O cliente abre **sempre** no launcher — a interface **não depende do servidor**:
login, perfil, avatares e o catálogo em cache continuam funcionando com o
master fora do ar (aparece “offline” e o botão **Conectar servidor**).

| Aba | O que faz |
|-----|-----------|
| **Jogar** | catálogo de mundos, busca, prévia do avatar e **Entrar no mundo** |
| **Perfil** | dados da conta (nome, email, bio), avatares salvos, **Usar** e **Editar** |
| **Avatar** | editor embutido: pele, camisa, calça, chapéu e nome — **Salvar avatar** |
| **Conta** | login (email ou apelido) e criação de conta, mais conexão direta por host/porta/token |

Ao entrar num mapa, o cliente baixa o `.loom` do master, monta os blocos, abre a
sala UDP e troca para a tela do jogo. **Esc** no jogo volta ao launcher (não
fecha o cliente). Falhas de rede aparecem na tela de carregamento com o motivo.

O que só funciona com servidor: login/cadastro, lista de mundos e entrada numa
sala. O resto (inclusive editar avatares em cache) roda offline.

## Rede
* O jogo **não abre porta própria**: o controlador mantém uma ponte UDP local
  em `127.0.0.1:7788` e o engine fala com ela (`LOOMIA_BRIDGE_PORT`).
* HTTP (login, avatares, catálogo, mapa): `src/http.lua` usa o `curl` do
  sistema. O cliente lê o corpo da resposta e envia o cookie de sessão; por
  isso o master expõe `/api/client/login` e `/api/client/register`, que
devolvem o token no corpo (o engine não lê cabeçalhos).
* O protocolo completo (mensagens, `ack`, física) está em
  [`docs/protocolo.md`](../docs/protocolo.md) e é validado por
  `loomia-gameserver/protocol_golden.json` com testes dos dois lados.

## Testes
```bash
lua loomia-client-lovr/tests/protocol_spec.lua    # protocolo × golden Go
lua loomia-client-lovr/tests/physics_spec.lua     # física × golden Go
lua loomia-client-lovr/tests/client_spec.lua      # transição loading→game, modo offline
lua loomia-client-lovr/tests/launcher_spec.lua    # clique no launcher (cartões, abas, editor)
lua loomia-client-lovr/tests/http_spec.lua http://127.0.0.1:8080  # login/avatar/catálogo/sala
lua loomia-client-lovr/tests/render_launcher.lua /tmp  # gera PNGs das telas
```

## Bandeja do sistema
O helper (`appimage/tray/loomia-tray.c`) fala **StatusNotifierItem por DBus**,
que é o que Cinnamon, GNOME, KDE, XFCE e MATE usam. O XEmbed antigo não
funcionava nesses desktops porque o `_NET_SYSTEM_TRAY_S0` não tem dono ali
(`xprop -root` responde “not found”). Sem libdbus/GTK: o binário implementa o
mínimo do protocolo (AUTH EXTERNAL com UID em hex ASCII + `RequestName` +
`RegisterStatusNotifierItem` + `GetAll` com o ícone ARGB).

Teclas do jogo: `WASD`, `Espaço`, botão direito (câmera), roda (zoom), `V`
(1ª/3ª pessoa), `T`/`Enter` (chat), `R` (respawn), `F1` (diagnóstico).
