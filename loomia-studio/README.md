# Loomia Studio

Editor visual de mapas `.loom` — a peça de criação do Loomia. Usa a **mesma
engine do cliente** (GLFW + OpenGL 3.3 + `WorldRenderer`) e o mesmo formato de
arquivo (`engine/world.h`), então o mapa que você salva aqui é o mapa que o
jogo roda.

## Compilar

```bash
cmake -B build && cmake --build build -j4
./build/loomia-studio            # tela de boas-vindas
./build/loomia-studio mapa.loom  # abre direto
```

## Empacotar como .AppImage

```bash
# precisa do linuxdeploy no PATH
./appimage/build.sh   # gera dist/loomia-studio*.AppImage
```

## A tela de boas-vindas

Quatro templates prontos: **Baseplate**, **Ilhas Flutuantes**, **Obby
(Parkour)** e **Sala de Exposição** (um bloco de cada material). Clique em um
para começar, ou use **"Abrir .loom"** para carregar um mapa existente
(usa o diálogo `zenity` da sua distro; o mesmo diálogo aparece no salvar).

## A tela do editor

- **Barra inferior**: ferramentas `1 Add`, `2 Select`, `3 Move`, `4 Scale`,
  `5 Paint`, `6 Erase` + paleta de 10 cores.
- **Clique esquerdo** aplica a ferramenta no mundo (Add coloca o bloco na face
  que você clicou; Paint troca cor+material; Erase apaga).
- **Botão direito**: orbita a câmera. **Scroll**: zoom.
- **Linha de status**: mostra `*` quando há mudanças não salvas.

## Atalhos

| Tecla | Ação |
|---|---|
| `1..6` | troca de ferramenta |
| `Ctrl+S` | salvar (pergunta onde se nunca salvou) |
| `Ctrl+N` | novo mapa (Baseplate) |
| `Ctrl+Z` | desfazer |
| `Ctrl+Shift+Z` / `Ctrl+Y` | refazer |
| `Ctrl+D` | duplicar bloco selecionado |
| `Delete` / `Backspace` | apagar bloco selecionado |
| `G` | arredondar posição do bloco (snap) |
| `F` | focar câmera no bloco selecionado |

## Formato .loom

O arquivo é JSON legível e versionável:

```json
{
  "id": "sky-islands",
  "name": "Ilhas Flutuantes",
  "spawn": [0, 1.5, 6],
  "sky_color": [120, 170, 235],
  "blocks": [
    { "position": [0, -0.5, 0], "size": [20, 1, 20], "color": [100, 160, 60], "material": "grass", "collidable": true }
  ],
  "version": 1
}
```

Itens, NPCs e scripts são opcionais e preservados pelo editor.
