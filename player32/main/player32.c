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
#include "esp_task_wdt.h"
#include "esp_log.h"

#include "esp_vfs_fat.h"

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


void heartbeat_task(void *pvParameters)
{
    while (1) {
        ESP_LOGI(TAG, "Heartbeat: test is alive: %lld ms", esp_timer_get_time()/1000);
        vTaskDelay(pdMS_TO_TICKS(30000));  // 30 seconds
    }
}

void generator_task(void *pvParameters)
{

    ESP_LOGI(TAG, "Generator: task init");

    // subscribe the task to the watchdog so I can kick it later.
    // so far not really doing TaskDelays here.

    esp_task_wdt_add(NULL);

    play_sine_wave(440.0f, 0.75f);

    vTaskDelete(NULL);
}

void es8388_player_task(void *pvParameters)
{

    ESP_LOGI(TAG, "Es8388 player: task init");

    wav_reader_state_t *wav_state = (wav_reader_state_t *)pvParameters;

    // don't think I need this now, because this task isn't CPU hard looped -
    // the ringbuf or the i2s routes should kick it?
    // esp_task_wdt_add(NULL);

    do {
        ESP_LOGI(TAG, "Starting WAV file read");
        play_es8388_wav(wav_state);
        ESP_LOGI(TAG, "ENDING WAV file read");
    } while (1);

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
        ESP_LOGE(TAG, "ES8388 init failed: %d",(int) ret);
    }

    ret = es8388_start(ES_MODULE_DAC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8388 start failed: %d",(int) ret);
    }

    // 
    ret = es8388_set_volume(80);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8388 set volume failed: %d",(int) ret);
    }

    // start a heartbeat task so I can tell everything's OK
    xTaskCreate(heartbeat_task, "heartbeat_task", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);

    // ret = init_i2s_std();
    // if (ret != ESP_OK) {
    //     ESP_LOGW(TAG, "I2S generatorES8388 init failed: %d",(int) ret);
    // }




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

    // start the task that reads hte wav file.
    // I generally hate calloc but it's OK here.
    wav_reader_state_t *wav_state = calloc(1, sizeof(wav_reader_state_t));
    if (wav_state == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for header");
    }
    wav_state->filepath = &music_filename[0];

     if (ESP_OK != wav_reader_init(wav_state)) {
        ESP_LOGE(TAG, "Could not initialize wav reader ");
     }

    // TODO: need to pull the code that creates the ring buffer and reads the wav header
    xTaskCreate(wav_reader_task, "wav_reader_task", 4096, (void *) wav_state, 7, NULL);

    // TODO: since we have information about the file, we should either set the ES8388 correctly,
    // or validate that it is correct

    // using the wav ringbuf, play the contents to the DAC
    // lower priority than the file reader
    xTaskCreate(es8388_player_task, "es8388_player_task", 4096, (void *) wav_state, 8, NULL);

    // start an audio play task generating a tone so I can see if the init is OK
    // This is a breadcrumb. Remove other uses of the 8388 if you want to see if
    // this is still working.
    // xTaskCreate(generator_task, "generator_task", 4096, NULL, 7, NULL);


#if 0
    for (int i=0; i<100; i++) {
        if ( test_sd_read_speed_vfs(music_filename) != ESP_OK ) {
            ESP_LOGE(TAG, " READ SPEED FAILED pass %d ",i);
        }
        else {
            ESP_LOGI(TAG, " READ SPEED SUCCESS: pass %d",i);
        }

        // dump_tasks();
    }
#endif

    vTaskDelay(pdMS_TO_TICKS(10000));
    // free(music_filename);
    esp_restart();

}