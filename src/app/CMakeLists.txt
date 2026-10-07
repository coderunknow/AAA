add_executable(mistpine main.cpp app.cpp)
target_link_libraries(mistpine PRIVATE aaa_render aaa_platform)
aaa_target_defaults(mistpine)
add_dependencies(mistpine aaa_shaders)

if(EMSCRIPTEN)
  set_target_properties(mistpine PROPERTIES OUTPUT_NAME "index" SUFFIX ".html")
  set(AAA_WEB_SHELL "${CMAKE_SOURCE_DIR}/web/shell.html")
  set_property(TARGET mistpine APPEND PROPERTY LINK_DEPENDS "${AAA_WEB_SHELL}")
  target_link_options(mistpine PRIVATE
    "--shell-file=${AAA_WEB_SHELL}"
    "--preload-file=${CMAKE_BINARY_DIR}/assets@/assets"
    "-sMAX_WEBGL_VERSION=2" "-sMIN_WEBGL_VERSION=2"
    "-sALLOW_MEMORY_GROWTH=1" "-sINITIAL_MEMORY=256MB" "-sSTACK_SIZE=1MB"
    "-sENVIRONMENT=web" "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','stringToUTF8','lengthBytesUTF8']"
    "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']")
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # (No -gsource-map: needs llvm-dwarfdump, absent from the sandbox LLVM; DWARF from -g is kept.)
    target_link_options(mistpine PRIVATE "-sASSERTIONS=1")
  elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    target_link_options(mistpine PRIVATE "--profiling-funcs")
  endif()
else()
  # Native: copy-free asset lookup — the binary loads <build>/assets via SDL_GetBasePath()/../assets.
  target_compile_definitions(mistpine PRIVATE AAA_ASSET_DIR="${CMAKE_BINARY_DIR}/assets")
endif()
