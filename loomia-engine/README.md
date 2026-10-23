# LoomiaEngine

Engine do cliente Loomia: um fork do [LOVR 0.19](https://lovr.org) (Lua + GLFW +
C) com um módulo novo de rede.

* Nome do projeto: `loomiaengine` (o alvo interno continua `lovr`).
* Namespace exposto ao jogo: **`loomia`** — o cliente chamaria `loomia.udp`;
  nesta cópia o módulo é `require('udp')`.
* O cliente Lua em [`../loomia-client-lovr`](../loomia-client-lovr) é o jogo.

## O módulo `udp`

```lua
local udp = require('udp')

assert(udp.connect('127.0.0.1', 7788))     -- socket UDP da sessão
udp.send('{"t":"hello","token":"abc"}')    -- datagrama para o servidor
local data, from = udp.receive()           -- nil, "timeout" quando vazio
print(from)                                -- "127.0.0.1:7788"
print(udp.port())                          -- porta local
udp.close()
```

* Não bloqueante: `receive()` devolve `nil, "timeout"` quando não há datagrama,
  então pode ser chamado uma vez por frame.
* `send(data[, host, port])` permite mandar para outro endereço sem reconectar.
* Buffer de 1 MB de cada lado; datagramas até 64 KB.
* Fecha o socket quando o estado Lua termina (`luax_atexit`).

## Build

Dependências (Debian/Ubuntu): `cmake`, `g++`, `libgl1-mesa-dev`, `libx11-dev`,
`libxrandr-dev`, `libxi-dev`, `libxcursor-dev`, `libxinerama-dev`,
`libx11-xcb-dev`, `libglslang-dev` (ou o `glslang` vendorizado).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
      -DLOVR_USE_VULKAN=OFF -DLOVR_USE_STEAM_AUDIO=OFF \
      -DLOVR_ENABLE_HEADSET=OFF -DLOVR_ENABLE_PHYSICS=OFF
cmake --build build -j"$(nproc)"
```

O binário sai em `build/` e é copiado para o AppImage como
`usr/bin/loomiaengine` pelo `loomia-client-lovr/appimage/build.sh`.

### Ajustes desta cópia (ambiente sem os pacotes `-dev`)

| Arquivo                        | Por quê                                                        |
|--------------------------------|----------------------------------------------------------------|
| `deps/xinerama-stub/`          | `libxinerama-dev` ausente: só os protótipos faltavam (o GLFW carrega a lib em runtime) |
| `deps/x11-stub/X11/Xlib-xcb.h` | `libx11-xcb-dev` ausente: declara `xcb_connection_t`             |
| `CMakeLists.txt` (link)        | linka `libX11-xcb.so.1` direto quando não há o symlink de dev    |
| `CMakeLists.txt` (backend)     | permite OpenGL (`LOVR_GL`) quando Vulkan/WebGPU estão OFF        |
| `CMakeLists.txt` (Lua)         | Lua 5.4 precisa de `lctype.c`/`lcorolib.c` na lista de fontes    |
| `src/api/l_thread.c`           | `lua_dump` do Lua 5.4 tem um parâmetro a mais                    |

Com os pacotes `-dev` instalados os stubs são ignorados (os headers do sistema
têm prioridade) e nada muda para quem builda "normalmente".

### Estado do build neste ambiente

A configuração e a compilação dos módulos novos (`udp`) passam; o **link final**
ainda depende de um backend de GPU completo. O build usou
`-DLOVR_USE_VULKAN=OFF` e precisou incluir `src/core/gpu_gl.c` como backend
OpenGL (`LOVR_GL`) — se o seu LOVR 0.19 trouxer o backend GL com outro nome,
ajuste essa linha. Para o AppImage definitivo, o caminho mais curto é buildar
com `libvulkan-dev`/`vulkan-headers` (Vulkan ligado, como o LOVR padrão).
