# Protocolo Loomia

Formato único das mensagens trocadas entre o **cliente** (`loomia-client-lovr`)
e o **game server** (`loomia-gameserver`).

Implementações que precisam bater:

| Lado   | Arquivo                                   |
|--------|-------------------------------------------|
| Go     | `loomia-gameserver/protocol.go`           |
| Lua    | `loomia-client-lovr/src/protocol.lua`     |
| HTTP   | `loomia-masterserver/server.js` (`/api/launch`) |

`loomia-gameserver/protocol_golden.json` guarda mensagens de exemplo e os dois
lados têm um teste que compara a leitura (e a serialização) com esse arquivo —
assim ninguém muda um dialeto sem o outro perceber.

## Transporte

- **UDP**, um datagrama JSON por mensagem (`{"t": "...", ...}`).
- O cliente fala com a sala pelo módulo nativo `udp` do engine
  (`loomia.udp.connect`), que também entrega o emissor de cada pacote.
- `loomia-gameserver -tcp` liga o mesmo protocolo sobre TCP (uma linha JSON por
  mensagem). Serve para diagnóstico e para os testes de integração.
- Datagramas acima de **64 KB** são rejeitados (o `welcome` costuma ter ~1 KB
  com 32 jogadores; o mapa **nunca** vai pelo UDP, vem por HTTP do master).

## Cliente → sala

```json
{"t":"hello","token":"abc","name":"Ana","appearance":{"username":"Ana","body_color":[234,190,25],"shirt_color":[15,120,190],"pants_color":[35,75,45],"hat":"cap"}}
{"t":"input","seq":12,"move_x":1,"move_z":0,"jump":false,"yaw":0.4}
{"t":"chat","text":"oi"}
{"t":"ping","id":7,"time":123.45}
{"t":"respawn"}
{"t":"bye"}
```

- `hello` abre a sessão. O **primeiro** pode se perder no UDP: o cliente
  reenvia o mesmo `hello` a cada 500 ms até receber `welcome`.
- `input` é enviado uma vez por tique (30 Hz). `move_x`/`move_z` estão em
  `[-1, 1]` (o servidor normaliza) e já rotacionados pelo yaw da câmera.
- `seq` cresce de 1 em diante e nunca é reutilizado na mesma sessão.

## Sala → cliente

```json
{"t":"welcome","id":3,"tick":120,"tick_rate":30,"map_id":"baseplate","map_name":"Baseplate","world_version":"baseplate-1a2b3c4d5e6f","spawn":[0,1,4],"sky_color":[150,200,240],"players":[{"id":4,"name":"Bob","p":[0,1,4],"v":[0,0,0],"yaw":0,"ground":true,"appearance":{...}}],"chat":[{"from":"Bob","text":"oi"}]}
{"t":"snapshot","tick":121,"players":[{"id":4,"ack":3,"p":[0,1,4],"v":[0,0,0],"yaw":0,"ground":true,"appearance":{...}}]}
{"t":"players","players":[{"id":3,"name":"Ana","appearance":{...}}]}
{"t":"chat","from":"Bob","text":"oi"}
{"t":"pong","id":7,"time":123.45,"server_time":1729000000.12}
{"t":"respawn","p":[0,1,4]}
{"t":"error","message":"token inválido"}
```

### Regras que o cliente precisa respeitar

1. **O snapshot não traz o próprio jogador.** Não existe `ack` de topo. A
   confirmação é o `ack` da entrada **dele** (`players[]` com `id == meu`), que
   é o último `input.seq` que o servidor aplicou. Enquanto não chega, o cliente
   assume `ack = 0` e mantém todos os inputs pendentes.
2. `welcome.you` **não existe** no protocolo: a posição inicial é `spawn`, e é o
   servidor que vai corrigir no primeiro `snapshot`.
3. `players[]` lista quem está na sala (inclui o próprio id).
4. `p`/`v` são `[x, y, z]` em unidades do mundo; `y` é a base do personagem
   (entre os pés), com a cabeça em `y + 3.0`.

## Mesma física nos dois lados

`loomia-gameserver/physics.go` e `loomia-client-lovr/src/physics.lua` são
cópias do mesmo algoritmo (o cliente prevê, o servidor decide). As constantes
estão em `docs/protocolo.md#constantes` e em `protocol_golden.json`:

| Constante              | Valor  |
|------------------------|--------|
| `tick_rate`            | 30 Hz  |
| `move_speed`           | 12.5   |
| `jump_speed`           | 40.0   |
| `gravity`              | -95.0  |
| `max_fall_speed`       | -90.0  |
| `character_radius`     | 0.55   |
| `character_height`     | 3.0    |
| `step_height`          | 0.6    |
| `snap_distance`        | 0.55   |
| `eye_height`           | 1.55   |
