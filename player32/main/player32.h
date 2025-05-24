#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"

#include "b_ringbuf.h"

enum FILETYPE_ENUM {
    FILETYPE_UNKNOWN,
    FILETYPE_MP3,
    FILETYPE_WAV
};

// size to read from file system
#define WAV_READER_READ_SIZE (8 * 1024) // Example size, adjust as needed
// size to make the ringbuf
#define WAV_READER_RINGBUF_SIZE (64 * 1024) // Example size, adjust as needed
// size to transmit to es8388 to ensure buffer size
#define ES8388_PLAYER_WRITE_SIZE (8 * 1024)

void print_task_list();
void print_task_stats();


// Structure that includes key information that you can get from a WAV header that is necessary
// for playback, like sample size, speed, rate, format.

typedef struct {
    char *filepath;
    int fd;

    b_ringbuf_handle_t ringbuf;

    bool done;
    
    // wav parameters
    uint16_t audio_format;      /** 1 is PCM, 3 is float */
    uint16_t num_channels;      /**< Number of audio channels (1=mono, 2=stereo) */
    uint32_t sample_rate;       /**< Sample rate in Hz (e.g., 44100, 48000) */
    uint16_t bits_per_sample;   /**< Bits per sample (16 or 24) */
    uint32_t data_size;         /**< Size of audio data in bytes */
    uint16_t block_align;       /**< Block alignment (channels * bits_per_sample / 8) */
    off_t data_offset;          /**< Offset to the beginning of the data chunk */
    uint32_t bytes_per_sec;       /**< Average bytes per second */
} wav_reader_state_t;


esp_err_t init_i2s_std(void);



void play_sine_wave(float frequency, float amplitude);
int music_filename_validate_vfs( const char *filename, enum FILETYPE_ENUM *filetype_o) ;
int music_filename_get_vfs( char **file_o, enum FILETYPE_ENUM *filetype_o);
esp_err_t init_sdcard_vfs(void);
esp_err_t test_sd_fread_speed_vfs(const char *filepath);
esp_err_t test_sd_read_speed_vfs(const char *filepath);

// output player for the es8388. Make sure it's initialized first using wav_reader.

esp_err_t play_es8388_wav(wav_reader_state_t *wav_state);

// wav_reader
esp_err_t wav_reader_header_read(wav_reader_state_t *state);
esp_err_t wav_reader_init(wav_reader_state_t *state );
void wav_reader_deinit(wav_reader_state_t *state);
void wav_reader_task(void* arg);

// tone_reader

esp_err_t tone_reader_init(wav_reader_state_t *state );
void tone_reader_deinit(wav_reader_state_t *state);
void tone_reader_task(void* arg);

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
