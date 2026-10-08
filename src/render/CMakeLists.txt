aaa_add_shaders(aaa_shaders SHADERS
  vs_terrain.sc fs_terrain.sc vs_terrain_shadow.sc
  vs_prop.sc fs_prop.sc fs_foliage.sc vs_prop_shadow.sc fs_shadow.sc fs_shadow_alpha.sc
  vs_character.sc fs_character.sc vs_character_shadow.sc
  vs_fullscreen.sc fs_sky.sc fs_tonemap.sc
  vs_water.sc fs_water.sc vs_fire.sc fs_fire.sc
  vs_ui.sc fs_ui.sc)

add_library(aaa_render STATIC
  renderer.cpp gpu_mesh.cpp shader_library.cpp atmosphere.cpp frustum.cpp
  terrain_renderer.cpp prop_renderer.cpp character_renderer.cpp water_renderer.cpp object_renderer.cpp
  ui_renderer.cpp)
target_link_libraries(aaa_render PUBLIC aaa_game aaa_procgen bgfx bx)
add_dependencies(aaa_render aaa_shaders)
aaa_target_defaults(aaa_render)
# stb_truetype (MIT / public domain, bundled with the pinned bgfx) builds the UI font
# atlas. SYSTEM include so its warnings never fail the project's -Werror build.
target_include_directories(aaa_render SYSTEM PRIVATE ${bgfx_cmake_SOURCE_DIR}/bgfx/3rdparty/stb)

# Runtime source assets (the OFL UI fonts) are copied next to the compiled shaders so
# they ship inside build/<preset>/assets and the web .data bundle.
add_custom_command(
  OUTPUT "${AAA_ASSET_OUT_DIR}/fonts/.stamp"
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/fonts" "${AAA_ASSET_OUT_DIR}/fonts"
  COMMAND ${CMAKE_COMMAND} -E touch "${AAA_ASSET_OUT_DIR}/fonts/.stamp"
  COMMENT "copy ui fonts"
  VERBATIM)
add_custom_target(aaa_runtime_assets DEPENDS "${AAA_ASSET_OUT_DIR}/fonts/.stamp")
add_dependencies(aaa_shaders aaa_runtime_assets)
