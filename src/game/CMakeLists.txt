add_library(aaa_game STATIC player.cpp camera.cpp character_animator.cpp time_of_day.cpp game.cpp save_game.cpp)
target_link_libraries(aaa_game PUBLIC aaa_world)
aaa_target_defaults(aaa_game)
