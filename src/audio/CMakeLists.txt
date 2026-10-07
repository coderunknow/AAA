# Pure procedural synthesis (no platform dependency; unit-tested natively).
add_library(aaa_audio STATIC soundscape.cpp)
target_include_directories(aaa_audio PUBLIC ${CMAKE_SOURCE_DIR}/src)
aaa_target_defaults(aaa_audio)
