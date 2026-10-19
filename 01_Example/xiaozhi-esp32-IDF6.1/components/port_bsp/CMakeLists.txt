# PhotoPainter components contain cyclic callbacks; retain their objects for IDF 6 linking.
idf_component_register(WHOLE_ARCHIVE

    SRCS 
    "i2c_bsp.cpp" 
    "display_bsp.cpp" 
    "sdcard_bsp.cpp" 
    "./src/multi_button/multi_button.c" 
    "button_bsp.c" 
    "led_bsp.c"
    "./codec_bsp.cpp"
    "./i2c_equipment.cpp"
    "./src/fonts/font14CN.c"
    "./src/fonts/font18CN.c"
    "./src/fonts/font22CN.c"
    "./src/fonts/font24.c"
    "traverse_nvs.cpp"
    PRIV_REQUIRES 
    nvs_flash
    esp_timer
    codec_board 
    espressif__zlib
    REQUIRES
    esp_driver_sdmmc
    fatfs
    app_bsp
    REQUIRES
    driver esp_driver_i2s esp_driver_i2c esp_driver_spi esp_driver_gpio
    INCLUDE_DIRS 
    "./" 
    "./src/multi_button"
    "./src/fonts"
    EMBED_FILES 
    "pcm_src/mode.pcm.zlib"
    "pcm_src/mode_1.pcm.zlib"
    "pcm_src/mode_2.pcm.zlib"
    "pcm_src/mode_3.pcm.zlib")
set_source_files_properties(
    sdcard_bsp.cpp
    PROPERTIES
    COMPILE_OPTIONS "-Wno-format-truncation"
)
