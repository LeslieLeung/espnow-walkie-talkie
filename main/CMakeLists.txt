if(NOT DEFINED IDF_VERSION_MINOR)
    set(IDF_VERSION_MINOR 0)
endif()
if(IDF_TARGET STREQUAL "esp32s31")
    if(IDF_VERSION_MAJOR LESS 6 OR (IDF_VERSION_MAJOR EQUAL 6 AND IDF_VERSION_MINOR LESS 1))
        message(FATAL_ERROR "esp32s31 (ESP-Mosaico) requires ESP-IDF >= 6.1 (S31 is still a preview target)")
    endif()
elseif(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    message(FATAL_ERROR "${IDF_TARGET} must be built with ESP-IDF 5.5.x; only esp32s31 uses IDF 6.1+")
endif()

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
    "soft_keys.cpp"
    "talk_controller.cpp"
    "ui.cpp"
    "ui_model.cpp"
    "vox.cpp")

set(WALKIE_PRIV_REQUIRES esp_event esp_netif esp_wifi nvs_flash webrtc_vad)
if(IDF_TARGET STREQUAL "esp32c3")
    list(APPEND WALKIE_SRCS "board_bsp_passport.cpp")
    list(APPEND WALKIE_PRIV_REQUIRES bsp_passport)
elseif(IDF_TARGET STREQUAL "esp32s31")
    list(APPEND WALKIE_SRCS "board_bsp_mosaico.cpp")
    list(APPEND WALKIE_PRIV_REQUIRES
        esp-mosaico-bsp
        esp_codec_dev
        esp_driver_gpio
        esp_driver_spi
        esp_lcd
        esp_lcd_co5300
        esp_lcd_touch
        esp_lcd_touch_cst9220)
else()
    list(APPEND WALKIE_SRCS "board_bsp_m5.cpp")
endif()

idf_component_register(
    SRCS ${WALKIE_SRCS}
    INCLUDE_DIRS "include"
    PRIV_REQUIRES ${WALKIE_PRIV_REQUIRES})

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
