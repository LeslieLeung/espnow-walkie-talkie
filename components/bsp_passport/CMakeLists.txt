# Vendored from FoloToy/ai-passport components/bsp (MIT).
# Only compiled for the esp32c3 (AI Passport) target; M5 targets use M5Unified.
if(IDF_TARGET STREQUAL "esp32c3")
    idf_component_register(
        SRCS "src/bsp_i2c.c"
             "src/bsp_display.c"
             "src/bsp_audio.c"
             "src/bsp_battery.c"
        INCLUDE_DIRS "include"
        REQUIRES driver esp_driver_i2c esp_driver_i2s esp_lcd esp_adc esp_codec_dev
    )
else()
    idf_component_register()
endif()
