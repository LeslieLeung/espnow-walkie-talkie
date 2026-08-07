idf_component_register(
    SRCS
        "app_main.cpp"
        "adpcm.cpp"
        "audio_jitter.cpp"
        "display_policy.cpp"
        "espnow_transport.cpp"
        "navigation.cpp"
        "presence.cpp"
        "protocol.cpp"
        "settings.cpp"
        "sticks3_bsp.cpp"
        "talk_controller.cpp"
        "ui.cpp"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES esp_event esp_netif esp_wifi nvs_flash)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
