# Formato de Mapa `.loom` — v1

Arquivo JSON único, versionado, usado por **editor, servidor e cliente**.
Mapas sem scripts e com scripts usam o mesmo formato (campo `scripts: []`
quando vazio) — não existe `.loom2`.

## Estrutura

```json
{
  "format": "loom",
  "version": 1,
  "id": "baseplate",
  "name": "Baseplate",
  "description": "Um piso limpo para começar do zero.",

  "settings": {
    "spawn": [0, 1.5, 6],
    "skyColor": [150, 200, 240]
  },

  "objects": [ /* ... */ ],
  "scripts": [ /* ... */ ],
  "logic":   [ /* ... */ ]
}
```

| Campo | Tipo | Obrigatório | Descrição |
|---|---|---|---|
| `format` | string | sim | Sempre `"loom"` |
| `version` | número | sim | `1` hoje; versões futuras mudam a estrutura |
| `id` | string | sim | Identificador do mapa (usado como URL) |
| `name` | string | não | Nome exibido (default: `id`) |
| `description` | string | não | Descrição curta |
| `settings` | objeto | sim | Propriedades do mundo |
| `objects` | lista | sim | Partes, itens e NPCs |
| `scripts` | lista | sim | Scripts Lua (`[]` se não houver) |
| `logic` | lista | sim | Eventos/ações estruturados (`[]` se não houver) |

## settings

| Campo | Tipo | Default | Descrição |
|---|---|---|---|
| `spawn` | `[x,y,z]` | `[0, 1.5, 6]` | Ponto de nascimento (metros) |
| `skyColor` | `[r,g,b]` | `[150,200,240]` | Cor do céu, RGB 0–255 |

## objects[]

```json
{
  "id": "floor",
  "type": "part",
  "shape": "box",
  "transform": {
    "position": [0, -1, 0],
    "rotation": [0, 0, 0],
    "size": [48, 1, 48]
  },
  "appearance": { "color": [60, 200, 170], "material": "stone" },
  "physics": { "anchored": true, "collision": true, "mass": 1 }
}
```

| Campo | Default | Valores |
|---|---|---|
| `id` | `obj<N>` | **Único e estável** — lógica referencia por ele |
| `type` | `"part"` | `part` \| `item` \| `npc` |
| `shape` | `"box"` | `box` \| `sphere` \| `cylinder` |
| `transform.position` | `[0,0,0]` | metros; centro da forma |
| `transform.rotation` | `[0,0,0]` | **graus**, ordem ZYX intrínseca (yaw, pitch, roll) |
| `transform.size` | `[1,1,1]` | extensão total; `box`: w×h×d, `sphere`: diâmetro replicado, `cylinder`: (diâmetro, altura, diâmetro) |
| `appearance.color` | `[200,200,200]` | RGB **0–255** |
| `appearance.material` | `"plastic"` | `plastic` \| `grass` \| `wood` \| `brick` \| `stone` \| `neon` |
| `physics.anchored` | `true` | `true` = imóvel |
| `physics.collision` | `true` | `false` = atravessável (decoração) |
| `physics.mass` | `1` | kg, `>= 0` (0 = sem peso dinâmico) |

### Formas

- **box**: caixa alinhada aos eixos; `size` = extensões totais.
- **sphere**: esfera; `size.x` = diâmetro (y/z replicados no carregamento).
- **cylinder**: cilindro em pé; `size.x` = diâmetro, `size.y` = altura.
- Novas formas entram como novos valores de `shape` — mapas antigos continuam válidos.

### Validação (compartilhada editor/servidor)

Erros (recusam o mapa no servidor): ids duplicados, shape/material/evento/ação
desconhecidos, tamanho `<= 0`, massa negativa ou não finita, script sem
`source`/`file`, referência de lógica a id inexistente.
Avisos (carregam com fallback): `name` vazio, material fora da lista vira
`plastic`.

## scripts[]

```json
{
  "id": "boost",
  "language": "lua",
  "target": "server",
  "source": "function onTouch(part) ... end"
}
```

| Campo | Default | Valores |
|---|---|---|
| `id` | — | único; obrigatório |
| `language` | `"lua"` | só `lua` hoje |
| `target` | `"server"` | `server` \| `client` \| `both` |
| `source` | — | código inline (ou `file`, um ou outro) |
| `file` | — | caminho de script externo relativo ao mapa |

Execução (servidor): ambiente **Lua restrito** — APIs apenas de um sandbox
 (`part`, `player`, `math`, `string`, `table`; sem `io`, `os`, `require`),
com limite de instruções por callback. Mudanças críticas de estado (dano,
teleporte, inventory) são **validadas no servidor** — o cliente nunca é
fonte de verdade.

## logic[]

```json
{ "id": "l1", "event": "onTouch", "objectId": "pad",
  "action": "teleport", "targetId": "pad", "value": 10 }
```

| Campo | Valores |
|---|---|
| `event` | `onTouch` \| `onEnter` \| `onExit` \| `onInteract` \| `onTimer` \| `onSpawn` |
| `objectId` | id de um objeto do mapa (validado) |
| `action` | `none` \| `teleport` \| `setVel` \| `kill` \| `color` \| `show` \| `hide` \| `playSound` |
| `targetId` | id opcional do objeto alvo da ação |
| `value` | número opcional (força, timer, índice de som) |

## Migração do formato legado

Arquivos antigos (`blocks`, `spawn`, `sky_color` no topo, `scripts` objeto de
strings) são **detectados automaticamente** pelo carregador e migrados para
v1: cada bloco vira um `object` (`box`, ids `obj1..objN`), scripts legados
viram entradas com ids `legacy_local/global/action/physics`. O editor avisa
"Formato antigo migrado para .loom v1." no status e salva já no formato novo.

## Exemplos

- **Válido sem scripts**: `tests` — caso 1 de `test_loom_format.cpp`
- **Válido com scripts/lógica**: caso 2
- **Inválidos** (versão futura, formato errado, JSON quebrado, referência
  inexistente): caso 4

Rodar os testes: `g++ -std=c++17 -I src -I ../loomia-client/engine
tests/test_loom_format.cpp src/loom_format.cpp ../loomia-client/engine/world.cpp
../loomia-client/engine/json.cpp -o /tmp/test-loom && /tmp/test-loom`
