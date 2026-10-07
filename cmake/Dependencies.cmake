# Third-party dependencies, pinned to exact commits and fetched from GitHub.
#
# To reuse an existing checkout (offline / sandbox), pass e.g.
#   -DFETCHCONTENT_SOURCE_DIR_BGFX_CMAKE=/path/to/bgfx.cmake
#   -DFETCHCONTENT_SOURCE_DIR_SDL3=/path/to/SDL
include(FetchContent)

# bgfx.cmake bundles bgfx + bx + bimg as submodules.
#   bgfx.cmake f2ea8fb0 (2026-10-06) -> bgfx cca91681, bx d86e4ea9, bimg 101b5b5f
set(AAA_BGFX_CMAKE_COMMIT f2ea8fb0438721d754aefa22b14fe161f948a383)
set(AAA_SDL3_TAG release-3.4.18)

FetchContent_Declare(bgfx_cmake
  GIT_REPOSITORY https://github.com/bkaradzic/bgfx.cmake.git
  GIT_TAG        ${AAA_BGFX_CMAKE_COMMIT}
  GIT_SUBMODULES_RECURSE ON
  GIT_SHALLOW    OFF)
FetchContent_Declare(SDL3
  GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
  GIT_TAG        ${AAA_SDL3_TAG}
  GIT_SHALLOW    ON)

# --- bgfx configuration -------------------------------------------------------
set(BGFX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BGFX_INSTALL        OFF CACHE BOOL "" FORCE)
set(BGFX_CUSTOM_TARGETS OFF CACHE BOOL "" FORCE)
set(BGFX_AMALGAMATED    ON  CACHE BOOL "" FORCE)
set(BX_AMALGAMATED      ON  CACHE BOOL "" FORCE)
set(BGFX_CONFIG_VIDEO   OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS ${AAA_BUILD_TOOLS} CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_SHADER   ${AAA_BUILD_TOOLS} CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_TEXTURE  ${AAA_BUILD_TOOLS} CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_GEOMETRY OFF CACHE BOOL "" FORCE)
set(BGFX_BUILD_TOOLS_BIN2C    OFF CACHE BOOL "" FORCE)
set(BGFX_WITH_WAYLAND OFF CACHE BOOL "" FORCE)

if(AAA_RENDERER_NOOP AND NOT EMSCRIPTEN)
  # Headless native build: satisfy bgfx.cmake's find_package(X11/OpenGL) with
  # empty stand-ins and compile only the Noop renderer.
  # CMAKE_FIND_PACKAGE_REDIRECTS_DIR is checked by find_package() before module mode.
  file(COPY "${CMAKE_CURRENT_LIST_DIR}/find-redirects-noop/x11-config.cmake"
            "${CMAKE_CURRENT_LIST_DIR}/find-redirects-noop/opengl-config.cmake"
       DESTINATION "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}")
  set(BGFX_CONFIG_RENDERER_NOOP_ONLY ON)
endif()

FetchContent_MakeAvailable(bgfx_cmake)

# Runtime texture *encoding* (etcpak, astc-enc, ...) is never needed by the game and its
# x86 intrinsics do not build for wasm; keep it out of the default target set.
if(EMSCRIPTEN AND TARGET bimg_encode)
  set_target_properties(bimg_encode PROPERTIES EXCLUDE_FROM_ALL ON)
endif()

if(BGFX_CONFIG_RENDERER_NOOP_ONLY AND TARGET bgfx)
  target_compile_definitions(bgfx PUBLIC
    BGFX_CONFIG_RENDERER_OPENGL=0 BGFX_CONFIG_RENDERER_OPENGLES=0
    BGFX_CONFIG_RENDERER_VULKAN=0)
endif()

# --- SDL3 -----------------------------------------------------------------------
if(AAA_BUILD_GAME)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON  CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  # bgfx owns the graphics context; SDL only provides window/input/audio/timing.
  set(SDL_RENDER OFF CACHE BOOL "" FORCE)
  set(SDL_GPU OFF CACHE BOOL "" FORCE)
  set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
  set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
  if(AAA_RENDERER_NOOP AND NOT EMSCRIPTEN)
    # Headless build machines have no X11/Wayland headers; windowing is not needed there.
    set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
  endif()
  FetchContent_MakeAvailable(SDL3)
endif()
