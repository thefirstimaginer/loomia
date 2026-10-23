# Cliente LOVR do Loomia (Lua)

Cliente 3D do jogo: renderiza o mundo retrô de blocos, captura teclado/mouse,
roda a **previsão local** da física e conversa com o **game server (Go)** por
UDP.

## Como o jogo abre

O site gera um link customizado e o sistema operacional entrega ao cliente:

```text
loomia://launch?server=127.0.0.1:7801&token=abc&map=baseplate&name=Ana&color=eabe19
```

O handler (`data/launcher/loomia-launch.sh`, criado pelo master) converte isso
em argumentos do LOVR:

```bash
lovr loomia-client-lovr server=127.0.0.1:7801 token=abc map=baseplate name=Ana color=eabe19
```

Sem link, o cliente abre o **menu** e permite digitar `host:porta` e o token.

## Rodar

```bash
lovr loomia-client-lovr                      # menu
lovr loomia-client-lovr server=127.0.0.1:7801 token=SEU_TOKEN name=Ana
```

Teclas:

| Tecla / ação        | Função                                    |
|---------------------|-------------------------------------------|
| `W A S D` / setas   | andar                                     |
| `Espaço`            | pular                                     |
| botão direito       | arrastar para girar a câmera              |
| roda do mouse       | zoom                                      |
| `V`                 | primeira/terceira pessoa                  |
| `T` / `Enter`       | chat (`Enter` envia, `Esc` cancela)       |
| `R`                 | respawn                                   |
| `F1`                | mostra o painel de diagnóstico            |

## Estrutura

```text
main.lua            entrada do LOVR: headset, loop, troca de estados (loading → menu → game)
conf.lua            configuração da janela
src/args.lua        parsing dos argumentos `chave=valor` e do link loomia://
src/net.lua         canal UDP: hello/input/snapshot/chat + medição de ping
src/netbridge.lua   processo auxiliar UDP ↔ linhas JSON (LuaSocket)
src/json.lua        JSON mínimo para quando o LOVR não tem decoder
src/physics.lua     física da previsão local (espelha loomia-gameserver/physics.go)
src/world.lua       baixa o mapa (.loom) do master e instancia os modelos
src/avatar.lua      avatar 2008 em blocos + etiqueta de nome (MakeText)
src/camera.lua      câmera orbital/primeira pessoa
src/hud.lua         HUD, lista de jogadores, chat e diagnóstico
src/states/         loading, menu, game
```

> **Física duplicada de propósito:** o cliente prevê com `src/physics.lua` e o
> servidor decide com `loomia-gameserver/physics.go`. As constantes (raio,
> velocidade, gravidade, degrau) precisam ser as mesmas nos dois arquivos.
