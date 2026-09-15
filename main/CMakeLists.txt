set(WALKIE_SRCS
    "app_main.cpp"
    "adpcm.cpp"
    "audio_jitter.cpp"
    "display_policy.cpp"
    "espnow_transport.cpp"
    "navigation.cpp"
    "presence.cpp"
    "protocol.cpp"
    "settings.cpp"
    "talk_controller.cpp"
    "ui.cpp"
    "ui_model.cpp"
    "vox.cpp")

set(WALKIE_PRIV_REQUIRES esp_event esp_netif esp_wifi nvs_flash webrtc_vad)
if(IDF_TARGET STREQUAL "esp32c3")
    list(APPEND WALKIE_SRCS "board_bsp_passport.cpp")
    list(APPEND WALKIE_PRIV_REQUIRES bsp_passport)
else()
    list(APPEND WALKIE_SRCS "board_bsp_m5.cpp")
endif()

idf_component_register(
    SRCS ${WALKIE_SRCS}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${WALKIE_PRIV_REQUIRES})

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
