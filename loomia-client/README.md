# Loomia Client (C++ · OpenGL 3.3 · SFML 3 + GLFW)

Cliente oficial do Loomia, reescrito em **C++** para rodar em PCs antigos com
**OpenGL** (o cliente anterior usava LOVR/Vulkan e não funcionava em GPUs
integradas antigas, como a Intel HD 4000).

```text
┌────────────── loomia-client ──────────────┐
│ Launcher (SFML)                            │
│  • login / conta                           │
│  • perfil e editor de avatar               │
│  • aguarda o "Jogar" no site (loomia://)   │
└───────────────────┬───────────────────────┘
                    │ link: server, token, map
                    ▼
┌────────────── jogo (OpenGL) ──────────────┐
│ baixa o .loom SÓ desta sessão → renderiza  │
│ física local + snapshots via UDP           │
└───────────────────────────┘
```

## Por que OpenGL + SFML + GLFW

* **OpenGL 3.3 core** funciona em GPUs de 2012+ (Mesa/Intel/AMD/NVIDIA) e em
  software rasterizer — validado nesta máquina (Intel HD 4000, GL 4.2).
* **SFML 3** cuida da janela, input, fontes e UI 2D do launcher.
* **GLFW** é usado na janela do jogo (contexto e input brutos).
* Os dois projetos de `new-engine-src` (GLFW+GLAD e o pacote SFML) são a base.

## Como abrir

```bash
# build (na raiz do repositório)
cmake -S loomia-client -B loomia-client/build -DCMAKE_BUILD_TYPE=Release
cmake --build loomia-client/build -j"$(nproc)"

# launcher
loomia-client/build/loomia-client

# com link do site (abre direto no mapa)
loomia-client/build/loomia-client "loomia://launch?server=127.0.0.1:7801&token=abc&map=baseplate"
```

O protocolo `loomia://` é entregue ao cliente como o primeiro argumento; o
launcher fica aberto esperando enquanto você navega no site.

## Estrutura

```text
loomia-client/
  CMakeLists.txt
  engine/          núcleo compartilhado (sem UI): mundo, GL, math, rede
    world.h        formato .loom (blocos, spawn, céu)
    gl.h/.cpp      carregador de funções OpenGL (GLAD embutido, sem depender de libs)
    math.h         Vec3/Mat4 mínimos
    shader.h/.cpp  compilação de shaders GLSL 330
    renderer.h/.cpp malha em lote do mundo (1 VBO para todos os cubos)
    avatar.h/.cpp   personagem em blocos + nametag
    physics.h/.cpp  física local (espelha loomia-gameserver/physics.go)
    net.h/.cpp      UDP (hello/input/snapshot/chat) + HTTP via curl
    proto.h        mensagens do protocolo (JSON)
  src/             launcher e jogo (SFML/GLFW)
```

## Compatibilidade

| Plataforma | Empacotamento |
|------------|---------------|
| Linux      | AppImage (`packaging/appimage.sh`) |
| Windows    | `.exe` (`packaging/windows.ps1`) |
| macOS      | `.app` (`packaging/macos.sh`) |
| Android    | `.apk` (`packaging/android/`, NDK + Gradle) |

Os scripts de empacotamento geram o artefato **se** as ferramentas da
plataforma estiverem disponíveis; caso contrário explicam o que falta.
