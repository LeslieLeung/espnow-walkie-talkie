cmake_minimum_required(VERSION 3.16)
project(walkie_host_tests LANGUAGES CXX)

enable_testing()

add_executable(walkie_host_tests
    host_tests.cpp
    ../main/adpcm.cpp
    ../main/audio_jitter.cpp
    ../main/display_policy.cpp
    ../main/navigation.cpp
    ../main/presence.cpp
    ../main/protocol.cpp
    ../main/talk_controller.cpp)
target_include_directories(walkie_host_tests PRIVATE ../main/include)
target_compile_features(walkie_host_tests PRIVATE cxx_std_17)
target_compile_options(walkie_host_tests PRIVATE -Wall -Wextra -Werror -pedantic)

add_test(NAME walkie_host_tests COMMAND walkie_host_tests)
