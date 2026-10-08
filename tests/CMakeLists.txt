add_executable(aaa_tests test_main.cpp test_math.cpp test_world.cpp test_game.cpp test_procgen.cpp test_save.cpp test_survival.cpp test_audio.cpp test_ui.cpp)
target_link_libraries(aaa_tests PRIVATE aaa_world aaa_game aaa_procgen aaa_audio aaa_ui)
aaa_target_defaults(aaa_tests)
add_test(NAME unit COMMAND aaa_tests)
