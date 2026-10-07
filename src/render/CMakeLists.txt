aaa_add_shaders(aaa_shaders SHADERS
  vs_terrain.sc fs_terrain.sc vs_terrain_shadow.sc
  vs_prop.sc fs_prop.sc fs_foliage.sc vs_prop_shadow.sc fs_shadow.sc fs_shadow_alpha.sc
  vs_character.sc fs_character.sc vs_character_shadow.sc
  vs_fullscreen.sc fs_sky.sc fs_tonemap.sc
  vs_water.sc fs_water.sc vs_fire.sc fs_fire.sc)

add_library(aaa_render STATIC
  renderer.cpp gpu_mesh.cpp shader_library.cpp atmosphere.cpp frustum.cpp
  terrain_renderer.cpp prop_renderer.cpp character_renderer.cpp water_renderer.cpp object_renderer.cpp)
target_link_libraries(aaa_render PUBLIC aaa_game aaa_procgen bgfx bx)
add_dependencies(aaa_render aaa_shaders)
aaa_target_defaults(aaa_render)
