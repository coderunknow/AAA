# Offline shader compilation with bgfx shaderc.
#
# Every shader in shaders/ is compiled for each profile the target needs and
# written to <build>/assets/shaders/<profile-dir>/<name>.bin, which is packaged
# with the other runtime assets (preloaded .data bundle on the web).
#
# Profile set per target (PROMPT §8.1) — shaderc emits every one of these from any
# host (dx11 = SM 5.0 bytecode, metal = MSL source wrapped in the bgfx bin):
#   web            : essl   (GLSL ES 3.00 -> WebGL2; the web backend is never WebGPU)
#   headless Noop  : essl   (Noop never executes shaders; the profile is a placeholder)
#   Linux native   : glsl (OpenGL primary), spirv (Vulkan fallback)
#   Windows native : dx11 (D3D11 primary), spirv + glsl (Vulkan / OpenGL fallbacks)
#   macOS native   : metal (Metal)

# Asset output locations. Defined before any early return below: the runtime-asset
# targets (the UI fonts) use AAA_ASSET_OUT_DIR even when shader compilation is
# skipped, and an empty value there makes them try to mkdir("/fonts").
set(AAA_SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(AAA_ASSET_OUT_DIR "${CMAKE_BINARY_DIR}/assets")

# Escape hatch for platforms where the pinned bgfx shaderc cannot be built.
#
# Dawn's tint (which bgfx's shaderc links unconditionally, but only uses for
# WGSL/WebGPU — a backend this project never uses) does not compile with the
# libc++ shipped on the current macOS runners. Rather than ship without a macOS
# artifact, the release pipeline compiles the shader profiles on a Linux runner
# (where the host shaderc emits every profile except dx11) and the macOS job
# builds with prebuilt shader binaries.
option(AAA_SKIP_SHADER_COMPILE "Use prebuilt shader binaries instead of compiling them" OFF)

if(AAA_SKIP_SHADER_COMPILE)
  message(STATUS "shader compilation SKIPPED: using prebuilt shader binaries from ${CMAKE_BINARY_DIR}/assets/shaders")
  function(aaa_add_shaders target)
    add_custom_target(${target} ALL COMMAND ${CMAKE_COMMAND} -E true)
  endfunction()
  return()
endif()

if(AAA_SHADERC)
  set(AAA_SHADERC_EXE "${AAA_SHADERC}")
  set(AAA_SHADERC_DEP "")
elseif(TARGET shaderc)
  set(AAA_SHADERC_EXE "$<TARGET_FILE:shaderc>")
  set(AAA_SHADERC_DEP shaderc)
else()
  message(FATAL_ERROR "No shaderc available. Build the 'tools' preset first and pass -DAAA_SHADERC=<path>.")
endif()

# AAA_SHADER_PROFILE_SET overrides the automatic per-platform selection so CI can
# compile every required profile (PROMPT §8.9): web | linux | macos | windows | host-all.
set(AAA_SHADER_PROFILE_SET "" CACHE STRING "Override the shader profile set (web|linux|macos|windows|host-all)")

if(NOT AAA_SHADER_PROFILE_SET STREQUAL "")
  if(AAA_SHADER_PROFILE_SET STREQUAL "web")
    set(AAA_SHADER_PROFILES "essl:300_es:linux")
  elseif(AAA_SHADER_PROFILE_SET STREQUAL "linux")
    set(AAA_SHADER_PROFILES "glsl:440:linux" "spirv:spirv:linux")
  elseif(AAA_SHADER_PROFILE_SET STREQUAL "macos")
    set(AAA_SHADER_PROFILES "metal:metal:osx")
  elseif(AAA_SHADER_PROFILE_SET STREQUAL "windows")
    set(AAA_SHADER_PROFILES "dx11:s_5_0:windows" "spirv:spirv:linux" "glsl:440:linux")
  elseif(AAA_SHADER_PROFILE_SET STREQUAL "host-all")
    # Everything this host's shaderc can emit. dx11 (SM 5.0) needs D3DCompiler support,
    # which the pinned bgfx.cmake shaderc only compiles in on Windows (PROMPT §8.1);
    # the Windows release job therefore compiles the dx11 profile with a Windows shaderc.
    set(AAA_SHADER_PROFILES "essl:300_es:linux" "glsl:440:linux" "spirv:spirv:linux" "metal:metal:osx")
    if(WIN32)
      list(APPEND AAA_SHADER_PROFILES "dx11:s_5_0:windows")
    endif()
  else()
    message(FATAL_ERROR "unknown AAA_SHADER_PROFILE_SET '${AAA_SHADER_PROFILE_SET}' (web|linux|macos|windows|host-all)")
  endif()
elseif(EMSCRIPTEN)
  set(AAA_SHADER_PROFILES "essl:300_es:linux")
elseif(AAA_RENDERER_NOOP)
  set(AAA_SHADER_PROFILES "essl:300_es:linux")
elseif(WIN32)
  set(AAA_SHADER_PROFILES "dx11:s_5_0:windows" "spirv:spirv:linux" "glsl:440:linux" "essl:300_es:linux")
elseif(APPLE)
  set(AAA_SHADER_PROFILES "metal:metal:osx" "essl:300_es:linux")
else()
  set(AAA_SHADER_PROFILES "glsl:440:linux" "spirv:spirv:linux" "essl:300_es:linux")
endif()
# NB: every native set also carries essl. The packaged-binary smoke test runs with
# --headless, and the Noop backend's profile dir is essl, so a package without it
# fails validation ("shader binary missing") even though the shipped artifacts
# themselves are correct. It is a few hundred kB of data a desktop player never loads.

# aaa_add_shaders(<target-name> SHADERS vs_a.sc fs_a.sc ...)
function(aaa_add_shaders target)
  cmake_parse_arguments(ARG "" "" "SHADERS" ${ARGN})
  set(outputs "")
  file(GLOB common_deps "${AAA_SHADER_SRC_DIR}/*.sh" "${AAA_SHADER_SRC_DIR}/varying.def.sc")
  foreach(sc ${ARG_SHADERS})
    get_filename_component(name "${sc}" NAME_WE)
    if(name MATCHES "^vs_")
      set(type vertex)
    elseif(name MATCHES "^fs_")
      set(type fragment)
    else()
      message(FATAL_ERROR "Shader ${sc} must start with vs_ or fs_")
    endif()
    # Optional per-shader varying file: <name>.varying.def.sc, else shared one.
    set(varying "${AAA_SHADER_SRC_DIR}/varying.def.sc")
    foreach(entry ${AAA_SHADER_PROFILES})
      string(REPLACE ":" ";" parts "${entry}")
      list(GET parts 0 dir)
      list(GET parts 1 profile)
      list(GET parts 2 platform)
      set(out "${AAA_ASSET_OUT_DIR}/shaders/${dir}/${name}.bin")
      add_custom_command(
        OUTPUT "${out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${AAA_ASSET_OUT_DIR}/shaders/${dir}"
        COMMAND "${AAA_SHADERC_EXE}" -f "${AAA_SHADER_SRC_DIR}/${sc}" -o "${out}"
                --type ${type} --platform ${platform} -p ${profile}
                --varyingdef "${varying}"
                -i "${AAA_SHADER_SRC_DIR}" -i "${bgfx_cmake_SOURCE_DIR}/bgfx/src"
                -O 3
        DEPENDS "${AAA_SHADER_SRC_DIR}/${sc}" ${common_deps} ${AAA_SHADERC_DEP}
        COMMENT "shaderc ${sc} -> ${dir}"
        VERBATIM)
      list(APPEND outputs "${out}")
    endforeach()
  endforeach()
  add_custom_target(${target} ALL DEPENDS ${outputs})
endfunction()
