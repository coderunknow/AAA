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
    "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_aaa_resume','_aaa_start_over']")
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # (No -gsource-map: needs llvm-dwarfdump, absent from the sandbox LLVM; DWARF from -g is kept.)
    target_link_options(mistpine PRIVATE "-sASSERTIONS=1")
  elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    target_link_options(mistpine PRIVATE "--profiling-funcs")
  endif()
else()
  # Native: no compile-time asset path. The binary discovers assets relative to the
  # executable at runtime (App::resolveAssetRoot), so packaged builds work from any CWD.
endif()

# Headless smoke runs (bgfx Noop renderer): full load -> play loop, including the QA scenarios.
if(AAA_RENDERER_NOOP AND NOT EMSCRIPTEN AND AAA_BUILD_TESTS)
  add_test(NAME smoke_day COMMAND mistpine --headless --frames 600 --hours 9)
  add_test(NAME smoke_night_camp COMMAND mistpine --headless --frames 600 --hours 22 --qa camp)
  add_test(NAME smoke_wolves COMMAND mistpine --headless --frames 600 --hours 23 --qa wolves)
  set_tests_properties(smoke_day smoke_night_camp smoke_wolves PROPERTIES TIMEOUT 600)
endif()
