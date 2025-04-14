// Ready PLAYER32
//
// LOUDFRAME project. Attempt to do the world's simplest ESP32
// read a file from the sd card, and play it on the correct I2S device
// in a loop!

// Author: Brian Bulkowski <brian@bulkowski.org> (c) 2025

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "esp_vfs_fat.h"
#include "ff.h" /// more direct flash functions

#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdmmc_defs.h"

#include "diskio_impl.h"
//#include "driver/sdspi_host.h"
//#include "driver/spi_common.h"

#include "driver/gpio.h"

#include "sys/stat.h"
#include "nvs_flash.h"
#include "esp_wifi.h"


// filesystem
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>

// local
#include "player32.h"
#include "es8388.h"

static const char *TAG = "player32";

static const char MP3_SUFFIX[] = ".mp3";
static const char WAV_SUFFIX[] = ".wav";
static const char PATH_PREFIX[] = "/sdcard";

enum FILETYPE_ENUM {
    FILETYPE_UNKNOWN,
    FILETYPE_MP3,
    FILETYPE_WAV
};

#define SD_MOUNT_POINT "/sdcard"

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


int music_filename_validate_vfs( const char *filename, enum FILETYPE_ENUM *filetype_o) {


    *filetype_o = FILETYPE_UNKNOWN;

    struct stat file_stat;

    if (stat(filename, &file_stat) != 0) {
        ESP_LOGW(TAG, "[] File %s does not exist",filename);
        return(-1);
    }
    if (! S_ISREG(file_stat.st_mode) ){
        ESP_LOGW(TAG, "[] File %s not a regular file",filename);
        return(-1);
    } 

    int lenstr = strlen(filename);

    if ( (lenstr > sizeof(MP3_SUFFIX)) &&
         (strncmp(filename + lenstr - sizeof(MP3_SUFFIX) + 1 , MP3_SUFFIX, sizeof(MP3_SUFFIX) -1) == 0 ) ) {
        ESP_LOGI(TAG, "[ MFV ] Found MP3: %s", filename);
        *filetype_o = FILETYPE_MP3;
    }
    // is it wav?
    else if ((lenstr > sizeof(WAV_SUFFIX)) &&
         (strncmp(filename + lenstr - sizeof(WAV_SUFFIX) + 1 , WAV_SUFFIX, sizeof(WAV_SUFFIX) -1) == 0 ) ) {
        ESP_LOGI(TAG, "[ MFV ] Found WAV: %s", filename);
        *filetype_o = FILETYPE_WAV;
    }
    else {
        ESP_LOGW(TAG, "[] File %s is not a supported encoder extension", filename);
        return(-1);
    }

    return(0);

}


int music_filename_get_vfs( char **file_o, enum FILETYPE_ENUM *filetype_o) {

    // pass pointers in correctly or get memory overwrites, not going to check
    // every little thing

    *file_o = NULL;
    *filetype_o = FILETYPE_UNKNOWN;

    char *filename = NULL;

    ESP_LOGI(TAG, "[MFG]: entered ");

    // let's see if I can autodetect the file format of the test stream on the sd card
    DIR *dir = opendir(PATH_PREFIX);
    if (!dir) {
        ESP_LOGI(TAG, "[E] can't open sd card for autodetect");
        return(-1);
    }

    struct dirent *ent;
    ESP_LOGI(TAG, "[ MFG ] enumerate SDcard");
    while ((ent = readdir(dir)) != NULL) {

        int lenstr = strlen(ent->d_name);

        ESP_LOGI(TAG, "[ MFG ] %s", ent->d_name);

        // is mp3?
        if ( (lenstr > sizeof(MP3_SUFFIX)) &&
             (strncmp(ent->d_name + lenstr - sizeof(MP3_SUFFIX) + 1 , MP3_SUFFIX, sizeof(MP3_SUFFIX) -1) == 0 ) ) {
            ESP_LOGI(TAG, "[ MFG ] Found MP3: %s", ent->d_name);
            if (filename) free(filename);
            filename = malloc(lenstr + sizeof(PATH_PREFIX) + 2);
            sprintf(filename, "%s/%s", PATH_PREFIX, ent->d_name);
            *filetype_o = FILETYPE_MP3;
        }
        // is it wav?
        else if ((lenstr > sizeof(WAV_SUFFIX)) &&
             (strncmp(ent->d_name + lenstr - sizeof(WAV_SUFFIX) + 1 , WAV_SUFFIX, sizeof(WAV_SUFFIX) -1) == 0 ) ) {
            ESP_LOGI(TAG, "[ MFG ] Found WAV: %s", ent->d_name);
            if (filename) free(filename);
            filename = malloc(lenstr + sizeof(PATH_PREFIX) + 2);
            sprintf(filename, "%s/%s", PATH_PREFIX, ent->d_name);
            *filetype_o = FILETYPE_WAV;
        }

    }
    ESP_LOGI(TAG, "[ 1.1] that's all the SDcard");
    closedir(dir);

    *file_o = filename;

    return(0);

}

//
// Note. This allows use of 'fread' and 'read'. However, fread does
// some really terrible buffering read ahead which is seriously no go for
// audio streaming. Use only with open / close / read .
//
// Note. This is initialized to use 4 channel MMC, This results in a rate of 2ms per 32kB read
// which is a rather awesome rate of about 16MB per second. However, it reuses a pin
// that's used during the init sequence, which means, depending on the SD card (physical card)
// it may not be possible to boot. In those cases, pop the card out, reboot, pop the
// card back in. Or, switch back to a width of 1.
// 
// This also requires setting the EFUSE for the flash voltage. Most cards use v3.3 flash
// and so does the AITHINKER AUDIO-KIT being used here, so you can just burn the value
// using the efuse safely, saving a pin.



esp_err_t init_sdcard_vfs(void)
{
    esp_err_t ret;

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 32 * 1024  // match format
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = 40000; // 40 MHz

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;  // Use 1 for 1-line mode, 4 for full 4-line if wired

    // Optional: customize pins if not using defaults
    // slot_config.clk = GPIO_NUM_14;
    // slot_config.cmd = GPIO_NUM_15;
    // slot_config.d0  = GPIO_NUM_2;
    // slot_config.d1-d3 only used in 4-bit mode ( 4, 12, 13 )

    sdmmc_card_t *card;
    ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount SDMMC filesystem: 0x%x", ret);
        return ret;
    }

    sdmmc_card_print_info(stdout, card);
    ESP_LOGI(TAG, "SD card mounted.");

    return ESP_OK;
}


esp_err_t test_sd_fread_speed_vfs(const char *filepath)
{
    esp_err_t err = ESP_OK;
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file: %s", filepath);
        return ESP_FAIL;
    }

    int target_speed_us = 450000;
    int read_sz = 32 * 1024;
    uint8_t *buf;
    size_t total_read = 0;

//    buf = malloc(read_sz);
// internal means it'll be in dram not flash. DMA is the other option and it's interesting
// because PSRAM or flash could take a DMA.
    buf = heap_caps_malloc(read_sz, MALLOC_CAP_DMA);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate read buffer, value %p ",(void*) buf);
        return ESP_FAIL;
    }

    // this causes EXCEPTIONAL SLOWDOWN and there are still glitches, which is FASCINATING
    // error code is never false.... it just falls back....
    // setvbuf(f, (char *) buf, _IONBF, read_sz);

    while (!feof(f)) {
        int64_t start_us = esp_timer_get_time();
        size_t r = fread(buf, 1, read_sz, f);
        if (r == 0) break;
        int64_t delta = esp_timer_get_time() - start_us;
        if (delta > (target_speed_us * 2) ) {
            ESP_LOGE(TAG, "READ SPIKE: %d bytes in %lld us offset %ud",r,delta, total_read);
            err = ESP_ERR_TIMEOUT;
        }
        else if (delta > target_speed_us) {
            ESP_LOGW(TAG, "READ SPIKE WARNING: %d bytes in %lld us offset %ud",r,delta, total_read);
        }

        //if (r != read_sz) {
        //    ESP_LOGI(TAG, "READ test: size not requested size is %zu should be %zu ",r, read_sz);
        //}

        if (ferror(f)) {
            ESP_LOGE(TAG, "READ test: file now in error state errno %d (%s)",errno, strerror(errno));
            err = ESP_FAIL;
            break;
        }
        // else {
        //     ESP_LOGI(TAG, "good read, %d bytes in %lld us",r,delta);
        // }
        // prints are slow?
        // delta = esp_timer_get_time() - start_us;

        // attempt a read rate of 
        int delay_us = target_speed_us - delta; 
        if (delay_us > 0) {
            vTaskDelay( pdMS_TO_TICKS(delay_us / 1000) );
        }
                        // ESP_LOGI(TAG, "read %d bytes in %lld us",r, delta);
        total_read += r;
    }

    free(buf);
    fclose(f);
    return err;

}

// the esp tuning guide specifically states that one should use
// read instead of fread to avoid some overhead.

esp_err_t test_sd_read_speed_vfs(const char *filepath)
{
    esp_err_t err = ESP_OK;
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to open file: %s errno %d", filepath,errno);
        return ESP_FAIL;
    }

    int target_speed_us = 180000;
    int read_sz = 32 * 1024;
    uint8_t *buf;
    size_t total_read = 0;

//    buf = malloc(read_sz);
// internal means it'll be in dram not flash. DMA is the other option and it's interesting
// because PSRAM or flash could take a DMA.
    buf = heap_caps_malloc(read_sz, MALLOC_CAP_DMA);
    if (buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate read buffer, value %p ",(void*) buf);
        return ESP_FAIL;
    }

    while (1) {

        int64_t start_us = esp_timer_get_time();
        ssize_t r = read(fd, buf, read_sz);
        if (r == 0) {
            ESP_LOGI(TAG, "end of file reached");
            break;
        }

        int64_t delta = esp_timer_get_time() - start_us;

        // just one little speed print
        if (total_read == 0) {
            ESP_LOGI(TAG, "read %d bytes in %lld us",r, delta);
        }

        if (delta > (target_speed_us * 2) ) {
            ESP_LOGE(TAG, "READ SPIKE: %d bytes in %lld us offset %ud",r,delta, total_read);
            err = ESP_ERR_TIMEOUT;
        }
        else if (delta > target_speed_us) {
            ESP_LOGW(TAG, "READ SPIKE WARNING: %d bytes in %lld us offset %ud",r,delta, total_read);
        }

        // add delay to hit read rate more generally
        int delay_us = target_speed_us - delta; 
        if (delay_us > 0) {
            vTaskDelay( pdMS_TO_TICKS(delay_us / 1000) );
        }
        // ESP_LOGI(TAG, "read %d bytes in %lld us",r, delta);
        total_read += r;
    };

    free(buf);
    close(fd);
    return err;

}


void heartbeat_task(void *pvParameters)
{
    while (1) {
        ESP_LOGI(TAG, "Heartbeat: test is alive: %lld ms", esp_timer_get_time()/1000);
        vTaskDelay(pdMS_TO_TICKS(30000));  // 30 seconds
    }
}

void generator_task(void *pvParameters)
{
    play_sine_wave(440.0f, 0.75f);

    vTaskDelete(NULL);
}


void dump_tasks(void) {

    const size_t line_size = 64;
    UBaseType_t task_count = uxTaskGetNumberOfTasks();
    size_t buf_size = task_count * line_size + 128;

    char* buf = malloc(buf_size);
    if (!buf) {
        printf("Out of memory for task list\n");
        return;
    }

    vTaskList(buf);
    printf("Task Name\tState\tPrio\tStack\tNum\n%s\n", buf);
    free(buf);
}


void app_main(void)
{
    ESP_LOGI(TAG, "Hello from Player32!");

    // have to init pins it seems? Not sure if these are needed, maybe not?
    //gpio_set_direction(GPIO_NUM_13, GPIO_MODE_OUTPUT);
    //gpio_set_level(GPIO_NUM_13, 1); // pull high before mount

    // set pullup for all kinds of things
    gpio_set_pull_mode(GPIO_NUM_2, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_NUM_4, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_NUM_12, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_NUM_13, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_NUM_14, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(GPIO_NUM_15, GPIO_PULLUP_ONLY);    

    // make sure wifi is all dead as much as possible for a perf test
    // get mode returns not init if not inited
    wifi_mode_t wifi_mode;
    if (ESP_OK == esp_wifi_get_mode(&wifi_mode)) {
        ESP_LOGI(TAG, "found wifi inited, uniniting");
        esp_wifi_stop();
        esp_wifi_deinit();
    }

    // configure the SD card
    for (int i = 0; i < 20; i++ ) {
        if (ESP_OK == init_sdcard_vfs()) break;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    //configure the es8388
    // ok, we should decide all the actually useful parts of a "codec config"
    // adc input channel
    // dac output channel
    // codec mode ()
    // audio_hal_codec_i2s_iface (i2s_iface)
    es_codec_config_t cfg = {
        .adc_input = ADC_INPUT_DISABLE,       /*!< set adc channel es_adc_input */
        .dac_output = (DAC_OUTPUT_LOUT_PWR | DAC_OUTPUT_ROUT_PWR | DAC_OUTPUT_LOUT1 | DAC_OUTPUT_ROUT1) ,     /*!< set dac channel es_dac_output */
        .codec_mode = ES_CODEC_MODE_DECODE,     /*!< select codec mode: adc, dac or both */
        .i2s_iface = {
            .mode = ES_MODE_SLAVE,      /* es_iface_mode !< Audio interface operating mode: master or slave */
            .fmt = ES_I2S_NORMAL,           /* es_uis_fmt !< Audio interface format */
            .samples = ES_RATE_44KHZ,                      /*!< Number of samples per second (sampling rate) */
            .bits = BIT_LENGTH_16BITS,     /*!< Audio bit depth */
        } /*!< set I2S interface configuration */
    };

    esp_err_t ret = es8388_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ES8388 init failed: %d",(int) ret);
    }

    // start a heartbeat task so I can tell everything's OK
    xTaskCreate(heartbeat_task, "heartbeat_task", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);

    ret = init_i2s_std();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2S generatorES8388 init failed: %d",(int) ret);
    }

    // start an audio play task generating a tone so I can see if the init is OK
    xTaskCreate(generator_task, "generator_task", 4096, NULL, 7, NULL);


#if 1
    // hardcode
    const char music_filename[] = "/sdcard/test-short.wav";
    enum FILETYPE_ENUM music_filetype;
    if (music_filename_validate_vfs(music_filename, &music_filetype) == ESP_OK) {
        ESP_LOGI(TAG, "Filename %s and Filetype %d detected", music_filename, (int) music_filetype);
    }
    else {
        ESP_LOGI(TAG, "no filename and filetype detected");
    }
#endif

#if 0
    // find one
    char *music_filename;
    enum FILETYPE_ENUM music_filetype;
    if (music_filename_get_vfs( &music_filename, &music_filetype) == ESP_OK) {
        ESP_LOGI(TAG, "Filename %s and Filetype %d detected", music_filename, (int) music_filetype);
    }
    else {
        ESP_LOGI(TAG, "no filename and filetype detected");
    }
#endif

#if 0
    char music_filename[] = "0:test-orig.mp3";
#endif

    for (int i=0; i<100; i++) {
        if ( test_sd_read_speed_vfs(music_filename) != ESP_OK ) {
            ESP_LOGE(TAG, " READ SPEED FAILED pass %d ",i);
        }
        else {
            ESP_LOGI(TAG, " READ SPEED SUCCESS: pass %d",i);
        }

        // dump_tasks();
    }


    vTaskDelay(pdMS_TO_TICKS(10000));
    // free(music_filename);
    esp_restart();

}