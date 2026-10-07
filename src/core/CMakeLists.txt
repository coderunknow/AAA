add_library(aaa_core STATIC log.cpp noise.cpp)
target_include_directories(aaa_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
aaa_target_defaults(aaa_core)
