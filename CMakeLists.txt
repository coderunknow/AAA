cmake_minimum_required(VERSION 3.24)
project(Mistpine VERSION 0.1.0 LANGUAGES C CXX)

# -----------------------------------------------------------------------------
# Mistpine (working title) — browser forest-survival vertical slice (C++20 / bgfx / Emscripten)
#
# Build flavours (see CMakePresets.json):
#   native  : development build. With AAA_RENDERER_NOOP=ON (sandbox/CI) the game
#             runs headless on bgfx's Noop renderer for smoke tests.
#   web     : Emscripten/WebAssembly build rendering through bgfx's WebGL2 path.
#   tools   : host-side tools (bgfx shaderc) used to compile shaders for any target.
# -----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AAA_BUILD_GAME   "Build the game executable" ON)
option(AAA_BUILD_TESTS  "Build unit tests" ON)
option(AAA_BUILD_TOOLS  "Build host tools (shaderc) — native only" OFF)
option(AAA_RENDERER_NOOP "Native only: build bgfx with only the Noop renderer (headless; no X11/GL needed)" OFF)
set(AAA_SHADERC "" CACHE FILEPATH "Path to a host bgfx shaderc executable (required for web builds)")

if(EMSCRIPTEN)
  set(AAA_PLATFORM_WEB ON)
  set(AAA_BUILD_TOOLS OFF)
  # WebAssembly SIMD (all current desktop browsers). Also required because bx adds
  # -msse4.2 (Emscripten reports an x86 processor), which em++ maps onto wasm SIMD.
  add_compile_options(-msimd128
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/cmake/emscripten_version_compat.h")
else()
  set(AAA_PLATFORM_WEB OFF)
endif()

if(AAA_BUILD_TESTS AND NOT AAA_PLATFORM_WEB)
  enable_testing()  # before any add_subdirectory so app smoke tests register too
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Core, platform-independent code (world generation, gameplay, procedural assets)
# never depends on bgfx or SDL, so tests can always build.
include(CompilerWarnings)
add_subdirectory(src/core)
add_subdirectory(src/world)
add_subdirectory(src/game)
add_subdirectory(src/procgen)
add_subdirectory(src/audio)

if(AAA_BUILD_GAME OR AAA_BUILD_TOOLS)
  include(Dependencies)
endif()

# The project VERSION (0.1.0) is the single source of truth for the app version,
# exposed to logs, --version and the title screen (PROMPT §8.9). Applied after the
# third-party includes above so it only reaches first-party targets.
add_compile_definitions(AAA_VERSION_STRING="${PROJECT_VERSION}")

if(AAA_BUILD_GAME)
  include(Shaders)
  add_subdirectory(src/render)
  add_subdirectory(src/platform)
  add_subdirectory(src/app)
endif()

if(AAA_BUILD_TESTS AND NOT AAA_PLATFORM_WEB)
  add_subdirectory(tests)
endif()
