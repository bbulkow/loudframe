#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "esp_log.h"

enum FILETYPE_ENUM {
    FILETYPE_UNKNOWN,
    FILETYPE_MP3,
    FILETYPE_WAV
};


esp_err_t init_i2s_std(void);

void play_sine_wave(float frequency, float amplitude);

int music_filename_validate_vfs( const char *filename, enum FILETYPE_ENUM *filetype_o) ;

int music_filename_get_vfs( char **file_o, enum FILETYPE_ENUM *filetype_o);

esp_err_t init_sdcard_vfs(void);

esp_err_t test_sd_fread_speed_vfs(const char *filepath);

esp_err_t test_sd_read_speed_vfs(const char *filepath);

// SDCARD pin config (taken from the board_defs file in esp-adf )
#define FUNC_SDCARD_EN            (1)
#define SDCARD_OPEN_FILE_NUM_MAX  5
#define SDCARD_INTR_GPIO          GPIO_NUM_34

#define ESP_SD_PIN_CLK            GPIO_NUM_14
#define ESP_SD_PIN_CMD            GPIO_NUM_15
#define ESP_SD_PIN_D0             GPIO_NUM_2
#define ESP_SD_PIN_D1             GPIO_NUM_4
#define ESP_SD_PIN_D2             GPIO_NUM_12
#define ESP_SD_PIN_D3             GPIO_NUM_13

#define PIN_NUM_MISO GPIO_NUM_2
#define PIN_NUM_MOSI GPIO_NUM_15
#define PIN_NUM_CLK  GPIO_NUM_14
#define PIN_NUM_CS   GPIO_NUM_13

#define SD_MOUNT_POINT "/sdcard"
