cmake_minimum_required(VERSION 3.16)
project(BetterGmodClone LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(BULLET REQUIRED IMPORTED_TARGET bullet)
find_package(OpenGL REQUIRED)
find_path(GLM_INCLUDE_DIR glm/glm.hpp REQUIRED)

# --- Dear ImGui (vendored) ---------------------------------------------------
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
add_library(imgui STATIC
  ${IMGUI_DIR}/imgui.cpp
  ${IMGUI_DIR}/imgui_draw.cpp
  ${IMGUI_DIR}/imgui_tables.cpp
  ${IMGUI_DIR}/imgui_widgets.cpp
  ${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp
  ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_link_libraries(imgui PUBLIC PkgConfig::SDL2)

# --- ENet (vendored, networking) ---------------------------------------------
set(ENET_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet)
add_library(enet STATIC
  ${ENET_DIR}/callbacks.c ${ENET_DIR}/compress.c ${ENET_DIR}/host.c ${ENET_DIR}/list.c
  ${ENET_DIR}/packet.c ${ENET_DIR}/peer.c ${ENET_DIR}/protocol.c ${ENET_DIR}/unix.c)
target_include_directories(enet PUBLIC ${ENET_DIR}/include)
target_compile_definitions(enet PRIVATE HAS_FCNTL=1 HAS_POLL=1 HAS_GETADDRINFO=1 HAS_GETNAMEINFO=1
  HAS_GETHOSTBYNAME_R=1 HAS_GETHOSTBYADDR_R=1 HAS_INET_PTON=1 HAS_INET_NTOP=1 HAS_MSGHDR_FLAGS=1 HAS_SOCKLEN_T=1)

# --- stb_vorbis (vendored, OGG decoding) --------------------------------------
add_library(stb_vorbis STATIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb/stb_vorbis.c)
target_compile_options(stb_vorbis PRIVATE -w)

# --- Game --------------------------------------------------------------------
file(GLOB GAME_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
add_executable(gmodclone ${GAME_SOURCES})
target_include_directories(gmodclone PRIVATE src ${GLM_INCLUDE_DIR})
target_compile_definitions(gmodclone PRIVATE GL_GLEXT_PROTOTYPES=1 GLM_ENABLE_EXPERIMENTAL
  GMOD_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")
target_link_libraries(gmodclone PRIVATE imgui enet stb_vorbis PkgConfig::SDL2 PkgConfig::BULLET OpenGL::GL m)

install(TARGETS gmodclone RUNTIME DESTINATION bin)
install(DIRECTORY assets DESTINATION share/better-gmod-clone)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(gmodclone PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
