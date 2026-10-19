
set(COMPONENT_PRIV_REQUIRE driver esp_codec_dev fatfs esp_lcd esp_driver_i2s esp_driver_gpio esp_driver_spi esp_driver_ledc esp_driver_sdmmc)

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.3")
list(APPEND COMPONENT_PRIV_REQUIRE esp_driver_i2c)
endif()

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_GREATER_EQUAL "5.3")
list(APPEND COMPONENT_PRIV_REQUIRE esp_driver_i2c)
endif()

# PhotoPainter components contain cyclic callbacks; retain their objects for IDF 6 linking.
idf_component_register(WHOLE_ARCHIVE
SRCS  "cfg_parse.c" "codec_board.c" "codec_init.c" "dummy_codec.c"
                             "lcd_init.c" "drv/tca9554.c"
                       INCLUDE_DIRS ./include
                       PRIV_INCLUDE_DIRS drv
                       REQUIRES "${COMPONENT_PRIV_REQUIRE}"
                       EMBED_TXTFILES "board_cfg.txt")
