# Offline shader compilation with bgfx shaderc.
#
# Every shader in shaders/ is compiled for each profile the target needs and
# written to <build>/assets/shaders/<profile-dir>/<name>.bin, which is packaged
# with the other runtime assets (preloaded .data bundle on the web).
#
#   web    : essl  (GLSL ES 3.00 -> WebGL2)
#   native : glsl (GLSL 4.40), spirv (Vulkan), essl; Noop-only builds use essl.

if(AAA_SHADERC)
  set(AAA_SHADERC_EXE "${AAA_SHADERC}")
  set(AAA_SHADERC_DEP "")
elseif(TARGET shaderc)
  set(AAA_SHADERC_EXE "$<TARGET_FILE:shaderc>")
  set(AAA_SHADERC_DEP shaderc)
else()
  message(FATAL_ERROR "No shaderc available. Build the 'tools' preset first and pass -DAAA_SHADERC=<path>.")
endif()

if(EMSCRIPTEN)
  set(AAA_SHADER_PROFILES "essl:300_es:linux")
elseif(AAA_RENDERER_NOOP)
  set(AAA_SHADER_PROFILES "essl:300_es:linux")
else()
  set(AAA_SHADER_PROFILES "glsl:440:linux" "spirv:spirv:linux" "essl:300_es:linux")
endif()

set(AAA_SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/shaders")
set(AAA_ASSET_OUT_DIR "${CMAKE_BINARY_DIR}/assets")

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
