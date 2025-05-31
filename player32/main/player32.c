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
#include "esp_heap_caps.h"
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
#include "es8388.h"
#include "maxbotics.h"
#include "player32.h"

static const char *TAG = "player32";

/* reminder about prioreitize
// low is low, so 0 is idle
// configMAX_PRIORITIES is the highest. 
*/

// Navigate to FreeRTOS Configuration: The relevant options are typically found under:
// Component config -> FreeRTOS

// Enable Statistics Gathering: Look for and enable these options:

//     Enable FreeRTOS to collect task stats (This might be named something similar, like Enable FreeRTOS stats registry)
//     Enable FreeRTOS to collect run time stats (If you want to use vTaskGetRunTimeStats)

// Enable Formatting Functions: To use vTaskList() and get the formatted table output, you specifically need to enable:

//     Enable FreeRTOS to include run time stats formatting functions (This or similarly named options might control both vTaskList and vTaskGetRunTimeStats output formatting).

// Enable FreeRTOS to include task list formatting functions (This specifically enables vTaskList).


#define TASK_LIST_BUFFER_SIZE 2048 // Adjust buffer size as needed
// stack allocated so be careful!

void print_task_list() {
    char pcWriteBuffer[TASK_LIST_BUFFER_SIZE];

    vTaskList(pcWriteBuffer);
    printf("Task List:\n");
    printf("name ******** state *** pri ****stk_hwm ***** taskid *******core\n");
    printf("%s\n", pcWriteBuffer);
    printf("*********************************************\n");
}

void print_task_stats() {
    char pcWriteBuffer[TASK_LIST_BUFFER_SIZE];


    // Generate the run time stats.
    // The table is written to the buffer, whose data is then printed to the serial port.
    vTaskGetRunTimeStats(pcWriteBuffer);

    printf("Task         Run Time ticks     percent   \n");
    printf("%s\n", pcWriteBuffer);
    printf("*********************************************\n");
}

void print_memory_info()
{
    ESP_LOGI(TAG, "--- Heap Memory Information ---");

    // Information for internal DRAM (most of the main internal heap, is DMA-capable)
    // MALLOC_CAP_DRAM is a good way to specifically target this primary pool.
    ESP_LOGI(TAG, "Internal 8-bit addressible DMA capable (DRAM):");
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);

    // You can combine capabilities too, e.g., Internal AND 32-bit addressable
    ESP_LOGI(TAG, "Internal 32-bit Addressable (IRAM):");
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);

    // Information for ALL internal memory that's part of the heap.
    // This includes DRAM + potentially IRAM data heap + potentially RTC FAST heap (if enabled).
    // Comparing this to DRAM shows if there are other internal pools contributing.
    ESP_LOGI(TAG, "Total Internal (DRAM + others like IRAM/RTC if in heap):");
    heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);

    // Information for external SPIRAM (if enabled and initialized)
    ESP_LOGI(TAG, "External SPIRAM:");
    heap_caps_print_heap_info(MALLOC_CAP_SPIRAM); // Or MALLOC_CAP_EXTRAM

    // Information for RTC FAST memory (if enabled for dynamic allocation)
    // Since you said this is false, this should show 0 total size.
    ESP_LOGI(TAG, "RTC FAST Memory (if enabled as heap):");
    heap_caps_print_heap_info(MALLOC_CAP_RTCRAM);

    // You can also query capabilities like MALLOC_CAP_DMA explicitly (this appears to always equal CAP_8BIT)
    // ESP_LOGI(TAG, "DMA Capable Memory (primarily DRAM):");
    // heap_caps_print_heap_info(MALLOC_CAP_DMA);

    ESP_LOGI(TAG, "-----------------------------");
}


// In your app_main or another initialization function:
// xTaskCreate(&print_task_list, "TaskListPrint", 4096, NULL, 5, NULL);

void heartbeat_task(void *pvParameters)
{
    while (1) {

        // print_task_list();
        //print_task_stats();
        // print_memory_info();

        ESP_LOGI(TAG, "Heartbeat: test is alive: %lld ms", esp_timer_get_time()/1000);
        vTaskDelay(pdMS_TO_TICKS(30000));  // 30 seconds
    }
}

void sd_read_speed_task(void *pvParameters) {

    const char music_filename[] = "/sdcard/test-short.wav";

    for (int i=0; i<1000; i++) {
        if ( test_sd_read_speed_vfs(music_filename) != ESP_OK ) {
            ESP_LOGE(TAG, " READ SPEED FAILED pass %d ",i);
        }
        else {
            ESP_LOGI(TAG, " READ SPEED SUCCESS: pass %d",i);
        }

        // dump_tasks();
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

// loop through setting the volume slowly
void volume_task(void *pvParameters)
{
    static const int top = 100;
    static const int bottom = 0;
    static const int step = 5;
    static const int delay_ms = 1000;
    while (1) {

        ESP_LOGI(TAG, "Volume: low and increasing (1s)");

        for (int i=bottom;i<top;i += step) {
            ESP_LOGI(TAG, "volume: %d",i);
            es8388_set_volume(i);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }

        ESP_LOGI(TAG, "Volume: high and decreasing (1s)");

        for (int i=top;i>bottom; i -= step) {
            ESP_LOGI(TAG, "volume: %d",i);
            es8388_set_volume(i);
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
        }
       
    }
    vTaskDelete(NULL);
}

// Read from a proximity sensor to see if there's
// distance, and if so, set the volume
void proximity_task(void *pvParameters)
{
    static const int top = 100;
    static const int bottom = 0;
    static const int step = 5;
    static const int delay_ms = 1000;

    /* Call Maxbotix function */
    maxbotix_init();

    while(1)
    {
    	int16_t count;

        uint16_t sample = maxbotix_get_latest();

        ESP_LOGI(TAG, "received sample %d", sample);

        int32_t age = maxbotix_get_age();
        float result = maxbotix_get_median(0.6f,8,32,&count);
        ESP_LOGI(TAG,"Median sample returned %f, sample count %d",(double)result,count);
        

        /* Wait delay for 2 second interval */
        vTaskDelay(pdMS_TO_TICKS(1000*2));
    }

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

    // Configure the ES8388 chip - INIT START and an initial set volume yeah?
    esp_err_t ret = es8388_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8388 init failed: %d",(int) ret);
    }

    ret = es8388_start(ES_MODULE_DAC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8388 start failed: %d",(int) ret);
    }

    // going to need to sort this out better at some point....
    // TODO: there appears to be a gain stage problem. I've got distortion in loud parts, but it's not the
    // master that's getting me.
    ret = es8388_set_volume(30);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ES8388 set volume failed: %d",(int) ret);
    }

    // start a heartbeat task so I can tell everything's OK
    xTaskCreatePinnedToCore(heartbeat_task, "heartbeat", 4096 /*stksz*/, NULL /*param*/, tskIDLE_PRIORITY + 1, 
        NULL /*tskreturn*/, 1 /*core*/);

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
#else
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

//
// PLAY THE FILE
// 1. alloc and get the shared state
// 2. Start the wav reader to get data into the ringbuf
// 3. start the es8388 player which will read from the ringbuf
//
#if 1
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

    // wav reader puts data in a ringbuf.
    // TODO: use something like xTaskNotifyGive to notify this main task when it's exiting.
    // note we want this priority higher than the player

    // Read from the file
#if 1
    xTaskCreatePinnedToCore(wav_reader_task, "wav_reader", 1024 * 6 /*stksz*/,(void *) wav_state, configMAX_PRIORITIES - 2, 
        NULL /*tskreturn*/, 1 /*core*/);
#else
    // Generate a tone, the same way - TEST CODE, but not yet debugged! TODO :-) 
    xTaskCreatePinnedToCore(tone_reader_task, "tone_reader", 1024 * 6 /*stksz*/,(void *) wav_state, configMAX_PRIORITIES - 2, 
        NULL /*tskreturn*/, 1 /*core*/);
#endif

    // TODO: since we have information about the file, we should either set the ES8388 correctly,
    // or validate that it is correct

    // using the wav ringbuf, play the contents to the DAC
    // lower priority than the file reader
    xTaskCreatePinnedToCore(es8388_player_task, "es8388_player", 1024 * 6, (void *) wav_state, configMAX_PRIORITIES - 4, 
                                NULL/*tskreturn*/, 1 /*core*/);
#endif

#if 0
    // start an audio play task generating a tone so I can see if the init is OK
    // This is a breadcrumb. Remove other uses of the 8388 if you want to see if
    // this is still working.
    xTaskCreate(generator_task, "generator_task", 4096, NULL, 7, NULL);
#endif

#if 0
/* THis tests read speed. Do we ahve spikes?
*/
    // using the wav ringbuf, play the contents to the DAC
    // lower priority than the file reader
    xTaskCreatePinnedToCore(sd_read_speed_task, "sd_read_speed", 1024 * 6, 0 /*param*/, configMAX_PRIORITIES - 3, 
                                NULL/*tskreturn*/, 1 /*core*/);
#endif

#if 0
    // little test code to make sure changing volume works, generally
    xTaskCreatePinnedToCore(volume_task, "volume_task", 1024 * 4, NULL, configMAX_PRIORITIES - 6, 
                                NULL/*tskreturn*/, 1 /*core*/);
#endif

#if 1
    // using a proxmity sensor, increase and decrease the volume based on what's around
    xTaskCreatePinnedToCore(proximity_task, "prox_task", 1024 * 4, NULL, configMAX_PRIORITIES - 6, 
                                NULL/*tskreturn*/, 1 /*core*/);
#endif

    // UGLY TODO! Need to have something other than a hard block
    vTaskDelay(portMAX_DELAY);
    // and a print to remember what I'm doing stuffs
    ESP_LOGE(TAG, "RESTARTING, end of main loop ");
    // free(music_filename);
    esp_restart();

}