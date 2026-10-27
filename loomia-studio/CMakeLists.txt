cmake_minimum_required(VERSION 3.16)
project(loomia-studio LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# O studio reusa o engine do cliente (world.h/.cpp, math.h, renderer.h/.cpp).
set(CLIENT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../loomia-client")

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
find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
  # Fallback: linka direto na lib do sistema.
  set(GLFW_LIB glfw)
else()
  set(GLFW_LIB glfw)
endif()

target_link_libraries(loomia-studio PRIVATE OpenGL::GL Freetype::Freetype glfw dl z)

if(UNIX AND NOT APPLE)
  target_compile_options(loomia-studio PRIVATE -Wall -Wextra)
endif()
