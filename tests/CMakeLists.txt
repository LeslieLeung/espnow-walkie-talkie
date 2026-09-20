cmake_minimum_required(VERSION 3.16)
project(walkie_host_tests LANGUAGES C CXX)

enable_testing()

set(WEBRTC_VAD_DIR ${CMAKE_CURRENT_LIST_DIR}/../components/webrtc_vad)
set(WEBRTC_VAD_SRCS
    ${WEBRTC_VAD_DIR}/common_audio/signal_processing/spl.c
    ${WEBRTC_VAD_DIR}/common_audio/vad/vad_core.c
    ${WEBRTC_VAD_DIR}/common_audio/vad/vad_filterbank.c
    ${WEBRTC_VAD_DIR}/common_audio/vad/vad_gmm.c
    ${WEBRTC_VAD_DIR}/common_audio/vad/vad_sp.c
    ${WEBRTC_VAD_DIR}/common_audio/vad/webrtc_vad.c)

    add_executable(walkie_host_tests
    host_tests.cpp
    ../main/adpcm.cpp
    ../main/audio_jitter.cpp
    ../main/display_policy.cpp
    ../main/navigation.cpp
    ../main/presence.cpp
    ../main/protocol.cpp
    ../main/soft_keys.cpp
    ../main/talk_controller.cpp
    ../main/ui_model.cpp
    ../main/vox.cpp
    ${WEBRTC_VAD_SRCS})
target_include_directories(walkie_host_tests PRIVATE
    ../main/include
    ${WEBRTC_VAD_DIR}
    ${WEBRTC_VAD_DIR}/common_audio/vad/include)
target_compile_features(walkie_host_tests PRIVATE cxx_std_17)
target_compile_options(walkie_host_tests PRIVATE -Wall -Wextra -Werror)
set_source_files_properties(${WEBRTC_VAD_SRCS} PROPERTIES COMPILE_OPTIONS "-Wno-error")
set_source_files_properties(host_tests.cpp ../main/adpcm.cpp ../main/audio_jitter.cpp
    ../main/display_policy.cpp ../main/navigation.cpp ../main/presence.cpp
    ../main/protocol.cpp ../main/soft_keys.cpp ../main/talk_controller.cpp
    ../main/ui_model.cpp ../main/vox.cpp
    PROPERTIES COMPILE_OPTIONS "-pedantic")

add_test(NAME walkie_host_tests COMMAND walkie_host_tests)
