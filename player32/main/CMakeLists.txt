idf_component_register(SRCS "wav_reader.c" "sdreader.c" "generator.c" "player32.c" "es8388_player.c"
                    INCLUDE_DIRS "."
                    REQUIRES sdmmc esp_timer fatfs nvs_flash esp_wifi es8388 driver esp_driver_i2s esp_ringbuf)
