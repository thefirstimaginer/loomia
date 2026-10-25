cmake_minimum_required(VERSION 3.16)
project(loomia-client LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Release por padrão (o jogo é jogado; debug só quando pedido).
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(LOOMIA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# ---------------------------------------------------------------------------
# Dependências
#
# SFML 3: o pacote de `new-engine-src/SFML` inclui .so e CMake config.
# GLFW:  se não houver no sistema, usamos o vendored do engine em
# `loomia-engine/deps/glfw` (já versionado neste repositório).
# ---------------------------------------------------------------------------

set(LOOMIA_SFML_DIR "${LOOMIA_ROOT}/new-engine-src/SFML" CACHE PATH "Pacote SFML (include/lib)")
if(EXISTS "${LOOMIA_SFML_DIR}/lib/cmake/SFML/SFMLConfig.cmake")
  set(SFML_DIR "${LOOMIA_SFML_DIR}/lib/cmake/SFML")
  find_package(SFML 3.0 COMPONENTS Graphics Window System REQUIRED)
else()
  find_package(SFML 3.0 COMPONENTS Graphics Window System REQUIRED)
endif()

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# GLFW: prioridade para o vendored (versão conhecida, sem depender do sistema).
set(LOOMIA_GLFW_DIR "${LOOMIA_ROOT}/loomia-engine/deps/glfw")
if(EXISTS "${LOOMIA_GLFW_DIR}/CMakeLists.txt")
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
  add_subdirectory("${LOOMIA_GLFW_DIR}" "${CMAKE_BINARY_DIR}/glfw" EXCLUDE_FROM_ALL)
else()
  find_package(glfw3 3.3 REQUIRED)
endif()

# SFML 3 usa ícone e imagem para a bandeja/janela; nada além do padrão.

# ---------------------------------------------------------------------------
# Núcleo do engine (compartilhado entre launcher e jogo)
# ---------------------------------------------------------------------------

file(GLOB ENGINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/engine/*.cpp")

# `glad.c` precisa ser compilado junto (C) e linkado com o executável:
# ele define os ponteiros das funções OpenGL usados pelo renderer.
# `glad.c` é compilado como C++ (o projeto não habilita a linguagem C):
# o header do GLAD já declara os símbolos como `extern "C"`.
set_source_files_properties("${CMAKE_CURRENT_SOURCE_DIR}/third_party/src/glad.c"
  PROPERTIES LANGUAGE CXX)

add_library(loomia-engine STATIC ${ENGINE_SOURCES}
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/src/glad.c")
target_include_directories(loomia-engine PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/engine"
  "${CMAKE_CURRENT_SOURCE_DIR}/src"
  # GLAD gerado (OpenGL 3.3 core) — inclui o loader `gladLoadGLLoader`.
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/include"
)
target_link_libraries(loomia-engine PUBLIC
  OpenGL::GL
  glfw
  Threads::Threads
)

# ---------------------------------------------------------------------------
# Executável único: `loomia-client` (launcher + jogo no mesmo processo)
# ---------------------------------------------------------------------------

file(GLOB CLIENT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

add_executable(loomia-client ${CLIENT_SOURCES})
target_link_libraries(loomia-client PRIVATE
  loomia-engine
  SFML::Graphics
  SFML::Window
  SFML::System
  OpenGL::GL
  glfw
  Threads::Threads
)

# RPATH para o SFML vendorizado (roda sem instalar nada).
set_target_properties(loomia-client PROPERTIES
  BUILD_RPATH "${LOOMIA_SFML_DIR}/lib"
  INSTALL_RPATH "\$ORIGIN/../lib"
)

# ---------------------------------------------------------------------------
# Testes (sem OpenGL/SFML: só o formato .loom)
# ---------------------------------------------------------------------------

enable_testing()
add_executable(loomia-tests tests/world_test.cpp)
target_link_libraries(loomia-tests PRIVATE loomia-engine)
add_test(NAME world_format COMMAND loomia-tests
  WORKING_DIRECTORY "${LOOMIA_ROOT}")

# Teste de rede UDP (manual: precisa de uma sala rodando).
add_executable(loomia-tests-udp tests/net_test.cpp)
target_link_libraries(loomia-tests-udp PRIVATE loomia-engine)

add_executable(loomia-tests-sfml-net tests/sfml_net_test.cpp)
target_link_libraries(loomia-tests-sfml-net PRIVATE loomia-engine SFML::Graphics SFML::Window SFML::System)

if(UNIX AND NOT APPLE)
  # libcurl é usada via `popen("curl ...")`; nada para linkar.
endif()

message(STATUS "Loomia Client: OpenGL 3.3 · SFML 3 · GLFW")
