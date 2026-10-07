add_library(aaa_platform STATIC platform.cpp input_mapper.cpp storage.cpp web_bridge.cpp audio_output.cpp)
target_link_libraries(aaa_platform PUBLIC aaa_game aaa_audio SDL3::SDL3-static)
aaa_target_defaults(aaa_platform)
