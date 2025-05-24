idf_component_register(
    SRCS 
        "player32.c" 
        "wav_reader.c" 
        "es8388_player.c" 
        "tone_reader.c"
        "sdreader.c" 
        "generator.c" 
    INCLUDE_DIRS "."
    REQUIRES sdmmc esp_timer fatfs nvs_flash esp_wifi es8388 b_ringbuf driver esp_driver_i2s esp_ringbuf)
