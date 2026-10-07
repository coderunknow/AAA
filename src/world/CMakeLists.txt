add_library(aaa_world STATIC heightfield.cpp world_layout.cpp terrain_generator.cpp scatter.cpp world.cpp shrine.cpp)
target_link_libraries(aaa_world PUBLIC aaa_core)
aaa_target_defaults(aaa_world)
