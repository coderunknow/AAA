add_library(aaa_procgen STATIC mesh_builder.cpp vegetation_meshes.cpp rock_meshes.cpp primitive_meshes.cpp textures.cpp
  structure_meshes.cpp)
target_link_libraries(aaa_procgen PUBLIC aaa_core aaa_world)
aaa_target_defaults(aaa_procgen)
