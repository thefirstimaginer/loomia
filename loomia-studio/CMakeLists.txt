cmake_minimum_required(VERSION 3.16)
project(loomia-studio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# O studio reusa o engine do cliente (world.h/.cpp, math.h, renderer.h/.cpp).
set(CLIENT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../loomia-client")
set(LOOMIA_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

# SFML 3: o pacote de `new-engine-src/SFML` inclui .so e CMake config.
set(LOOMIA_SFML_DIR "${LOOMIA_ROOT}/new-engine-src/SFML" CACHE PATH "Pacote SFML (include/lib)")
if(EXISTS "${LOOMIA_SFML_DIR}/lib/cmake/SFML/SFMLConfig.cmake")
  set(SFML_DIR "${LOOMIA_SFML_DIR}/lib/cmake/SFML")
  find_package(SFML 3.0 COMPONENTS Window REQUIRED)
else()
  find_package(SFML 3.0 COMPONENTS Window REQUIRED)
endif()

add_executable(loomia-studio
  src/main.cpp
  src/editor.cpp
  src/editor_ui.cpp
  src/editor_actions.cpp
  src/editor_gizmo.cpp
  src/editor_scripts.cpp
  src/editor_shell.cpp
  src/png_loader.cpp
  src/icon_atlas.cpp
  src/editor_lua.cpp
  src/editor_hierarchy.cpp
  src/editor_assets.cpp
  src/editor_testplay.cpp
  src/editor_play.cpp
  src/font.cpp
  src/loom_format.cpp
  src/templates.cpp
  "${CLIENT_ROOT}/engine/world.cpp"
  "${CLIENT_ROOT}/engine/json.cpp"
  "${CLIENT_ROOT}/engine/renderer.cpp"
  "${CLIENT_ROOT}/third_party/src/glad.c"
)

target_include_directories(loomia-studio PRIVATE
  src
  "${CLIENT_ROOT}/engine"
  "${CLIENT_ROOT}/third_party/include"
)

find_package(OpenGL REQUIRED)
find_package(Freetype REQUIRED)

target_link_libraries(loomia-studio PRIVATE OpenGL::GL Freetype::Freetype SFML::Window dl z)

if(UNIX AND NOT APPLE)
  target_compile_options(loomia-studio PRIVATE -Wall -Wextra)
endif()
