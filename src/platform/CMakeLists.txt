add_library(aaa_platform STATIC platform.cpp input_mapper.cpp storage.cpp web_bridge.cpp)
target_link_libraries(aaa_platform PUBLIC aaa_game SDL3::SDL3-static)
aaa_target_defaults(aaa_platform)
