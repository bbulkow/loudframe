idf_component_register(SRCS "sdreader.c" "generator.c" "player32.c"
                    INCLUDE_DIRS "."
                    REQUIRES sdmmc esp_timer fatfs nvs_flash esp_wifi es8388 driver esp_driver_i2s)
