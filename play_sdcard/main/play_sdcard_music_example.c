/* Play MP3 file from SD Card

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sys/stat.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"


// we want a set of decoders not just a single configured one
#include "mp3_decoder.h"
#include "wav_decoder.h"
#include "equalizer.h" // to cover the sins of bad speaker design if necessary

#include "esp_peripherals.h"
#include "periph_touch.h"
#include "periph_adc_button.h"
#include "periph_button.h"
#include "periph_sdcard.h"
#include "board.h"

// filesystem
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>

static const char *TAG = "PLAY_SDCARD";

static const char MP3_SUFFIX[] = ".mp3";
static const char WAV_SUFFIX[] = ".wav";
static const char PATH_PREFIX[] = "/sdcard";

enum FILETYPE_ENUM {
    FILETYPE_UNKNOWN,
    FILETYPE_MP3,
    FILETYPE_WAV
};

// audio element status strings, useful for debugging, see audio_element.h for enum
#if 0
static const char *AEL_COMMAND_STRINGS[] = {
    "NONE",
    "ERROR", /* depricated */
    "FINISH",
    "STOP",
    "PAUSE",
    "RESUME",
    "DESTROY",
    "CHANGE_STATE",
    "REPORT_STATUS",
    "REPORT_MUSIC_INFO",
    "REPORT_CODEC_FMT",
    "REPORT_POSITION"
};

static const char *AEL_STATUS_STRINGS[] = {
    "NONE",
    "ERROR_OPEN",
    "ERROR_INPUT",
    "ERROR_PROCESS",
    "ERROR_OUTPUT",
    "ERROR_CLOSE",
    "ERROR_TIMEOUT",
    "ERROR_UNKNOWN",
    "INPUT_DONE",
    "INPUT_BUFFERING",
    "OUTPUT_DONE",
    "OUTPUT_BUFFERING",
    "STATE_RUNNING",
    "STATE_PAUSED",
    "STATE_STOPPED",
    "STATE_FINISHED",
    "MOUNTED",
    "UNMOUNTED"
};
#endif


int music_filename_validate( const char *filename, enum FILETYPE_ENUM *filetype_o) {


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
        ESP_LOGI(TAG, "[ MFG ] Found MP3: %s", filename);
        *filetype_o = FILETYPE_MP3;
    }
    // is it wav?
    else if ((lenstr > sizeof(WAV_SUFFIX)) &&
         (strncmp(filename + lenstr - sizeof(WAV_SUFFIX) + 1 , WAV_SUFFIX, sizeof(WAV_SUFFIX) -1) == 0 ) ) {
        ESP_LOGI(TAG, "[ MFG ] Found WAV: %s", filename);
        *filetype_o = FILETYPE_WAV;
    }
    else {
        ESP_LOGW(TAG, "[] File %s is not a supported encoder extension", filename);
        return(-1);
    }

    return(0);

}


int music_filename_get( char **file_o, enum FILETYPE_ENUM *filetype_o) {

    // pass pointers in correctly or get memory overwrites, not going to check
    // every little thing

    *file_o = NULL;
    *filetype_o = FILETYPE_UNKNOWN;

    char *filename = NULL;

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


void app_main(void)
{
    // Example of linking elements into an audio pipeline -- START
    audio_pipeline_handle_t pipeline;
    audio_element_handle_t fatfs_stream_reader, i2s_stream_writer, mp3_decoder, wav_decoder, *music_decoder, equalizer;

    esp_log_level_set("*", ESP_LOG_DEBUG);
    esp_log_level_set(TAG, ESP_LOG_DEBUG);
    // key units I'm using
    esp_log_level_set("WAV_DECODER", ESP_LOG_DEBUG);
    esp_log_level_set("FATFS_STREAM", ESP_LOG_DEBUG);
    esp_log_level_set("I2S_STREAM", ESP_LOG_INFO);
    esp_log_level_set("AUDIO_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("AUDIO_ELEMENT", ESP_LOG_DEBUG);

    ESP_LOGI(TAG, "[ 1 ] Mount sdcard");
    // Initialize peripherals management
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    // Initialize SD Card peripheral (including mount?)
    audio_board_sdcard_init(set, SD_MODE_1_LINE);
    // Init keys too
    audio_board_key_init(set);

    // this helper iterates everything on the SD card, which will come in handy when we want to play
    // all the files in a loop or something
#if 0
    char *music_filename;
    enum FILETYPE_ENUM music_filetype;
    music_filename_get(&music_filename, &music_filetype);
    if (music_filename != NULL) {
        ESP_LOGI(TAG, "Filename %s and Filetype %d detected", music_filename, (int) music_filetype);
    }
    else {
        ESP_LOGI(TAG, "no filename and filetype detected");
    }
#else
    const char music_filename[] = "/sdcard/test-short.wav";
    enum FILETYPE_ENUM music_filetype;
    if (music_filename_validate(music_filename, &music_filetype) < 0) {
        ESP_LOGI(TAG, "Filename %s and Filetype %d detected", music_filename, (int) music_filetype);
    }
    else {
        ESP_LOGI(TAG, "no filename and filetype detected");
    }
#endif

    ESP_LOGI(TAG, "[ 2 ] Start codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);

    int player_volume = 50;
    audio_hal_set_volume(board_handle->audio_hal, player_volume);


    ESP_LOGI(TAG, "[3.0] Create audio pipeline for playback");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline_cfg.rb_size *= 4; // will this help stuttering?
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    ESP_LOGI(TAG, "[3.1] Create fatfs stream to read data from sdcard");
    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_cfg.task_core = 1;
    fatfs_cfg.task_prio = 22;
    fatfs_cfg.buf_sz *= 4;
    fatfs_cfg.out_rb_size *= 4;
    fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);

    ESP_LOGI(TAG, "[3.2] Create i2s stream to write data to codec chip");
    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_cfg.out_rb_size *= 4; // Increase buffer size
    i2s_cfg.task_core = 1;
    i2s_cfg.task_prio = 18;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);

    ESP_LOGI(TAG, "[3.3] Create mp3 decoder");
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_cfg.task_core = 1;
    mp3_cfg.task_prio = 20;
    mp3_decoder = mp3_decoder_init(&mp3_cfg);

    ESP_LOGI(TAG, "[3.4] Create wav decoder");
    wav_decoder_cfg_t  wav_dec_cfg  = DEFAULT_WAV_DECODER_CONFIG();
    wav_dec_cfg.task_core = 1;
    wav_dec_cfg.task_prio = 20;
    wav_decoder = wav_decoder_init(&wav_dec_cfg);

    ESP_LOGI(TAG, "[3.5] Create equalizer");
    equalizer_cfg_t eq_cfg = DEFAULT_EQUALIZER_CONFIG();
    eq_cfg.task_core = 1;
    eq_cfg.task_prio = 22;
    int set_gain[22];
    memset(set_gain, 0, sizeof(set_gain)); // set to no eq
    // low-cut for the sections the speaker can't reproduce
    set_gain[0] = set_gain[1] = set_gain[11] = set_gain[12] = -13;
    eq_cfg.set_gain =
        set_gain; // The size of gain array should be the multiplication of NUMBER_BAND (10) and number channels of audio stream data (thus these 22 values are correct for stereo). 
        // The minimum of gain is -13 dB.
    equalizer = equalizer_init(&eq_cfg);
    // The center frequencies of the equalizer are 31 Hz, 62 Hz, 125 Hz, 250 Hz, 500 Hz, 1 kHz, 2 kHz, 4 kHz, 8 kHz, and 16 kHz.

    if (music_filetype == FILETYPE_MP3) {
        music_decoder = &mp3_decoder;
    }
    else if (music_filetype == FILETYPE_WAV) {
        music_decoder = &wav_decoder;
    }
    else {
        ESP_LOGE(TAG, "Unknown file decoder type : %d", (int) music_filetype);
        music_decoder = NULL;
    }

    ESP_LOGI(TAG, "[3.6] Register all elements to audio pipeline");
    audio_pipeline_register(pipeline, fatfs_stream_reader, "file");
    audio_pipeline_register(pipeline, *music_decoder, "dec");
    audio_pipeline_register(pipeline, equalizer, "eq");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

#if 0
    ESP_LOGI(TAG, "[3.7] Link it together [sdcard]-->fatfs_stream-->music_decoder-->eq-->i2s_stream-->[codec_chip]");
    const char *link_tag[4] = {"file", "dec", "eq", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 4);
#else
    ESP_LOGI(TAG, "[3.7] Link it together [sdcard]-->fatfs_stream-->music_decoder-->i2s_stream-->[codec_chip]");
    const char *link_tag[3] = {"file", "dec", "i2s"};
    audio_pipeline_link(pipeline, &link_tag[0], 3);
#endif

    ESP_LOGI(TAG, "[3.8] Set file path for stream reader %s", music_filename);
    audio_element_set_uri(fatfs_stream_reader, music_filename);

    ESP_LOGI(TAG, "[4] Initialize keys on board");
    audio_board_key_init(set);

    ESP_LOGI(TAG, "[ 4.1 ] Set up  event listener");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    audio_event_iface_handle_t evt = audio_event_iface_init(&evt_cfg);

    ESP_LOGI(TAG, "[4.2] Listening event from all elements of pipeline");
    audio_pipeline_set_listener(pipeline, evt);

    ESP_LOGI(TAG, "[4.3] Listening event from peripherals");
    audio_event_iface_set_listener(esp_periph_set_get_event_iface(set), evt);

    ESP_LOGI(TAG, "[ 5 ] Start audio_pipeline");
    audio_pipeline_run(pipeline);
    // Example of linking elements into an audio pipeline -- END

    int64_t start_us = esp_timer_get_time();

    ESP_LOGI(TAG, "[ 6 ] Listen for all pipeline events");
    while (1) {


        // BB - seems like a lot of pops, put this a little more nice? 10ms delay
        // vTaskDelay(1);

        audio_event_iface_msg_t msg;

        esp_err_t ret = audio_event_iface_listen(evt, &msg, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "[ * ] Event interface error : %d errno %d ", ret, errno);
            continue;
        }

        if (msg.need_free_data) {
            ESP_LOGE(TAG, "[ * ] Warning! Leak! Received message that requires freeing of data, sourcetype %d cmd %d",msg.source_type,msg.cmd);
        }

        // for events from the music decoder.....
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) *music_decoder) {

            // if you get a music info, set the i2s stage clock to match
            if (msg.cmd == AEL_MSG_CMD_REPORT_MUSIC_INFO) {
                audio_element_info_t music_info = {0};
                audio_element_getinfo(*music_decoder, &music_info);

                ESP_LOGI(TAG, "[ * ] Receive decoder music info, sample_rates=%d, bits=%d, ch=%d",
                         music_info.sample_rates, music_info.bits, music_info.channels);

                audio_element_setinfo(i2s_stream_writer, &music_info);
                i2s_stream_set_clk(i2s_stream_writer, music_info.sample_rates, music_info.bits, music_info.channels);
                continue;
            }

            // if you get a stopped or finish, resume this file (set uri resets, doing an arbitrary to 0 is possible too)
            // a restart is interesting, because just about everything in the pipeline gets finished events.
            if ( (msg.cmd == AEL_MSG_CMD_REPORT_STATUS) &&
                 (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {

                ESP_LOGI(TAG, "[ * ] Decoder stopped or finished, restarting same");

                int64_t delta_us = esp_timer_get_time() - start_us;
                ESP_LOGI(TAG, "[ * ] clip delta: %d sec %d usec", (int) (delta_us / 1000000), (int) (delta_us % 1000000));
                if (delta_us > 10500000) {
                    ESP_LOGE(TAG, "[ E ] ERROR CLIP TOOK TOO LONG!!!");
                }

                // this took a lot of trial and error. Each step appears to be necessary.

                //ESP_LOGI(TAG, "[ * ] pipeline stop");
                //audio_pipeline_stop(pipeline);
                //ESP_LOGI(TAG, "[ * ] pipeline waitforstop");
                //audio_pipeline_wait_for_stop(pipeline);
                
                ESP_LOGD(TAG, "[ * ] pipeline terminate");
                audio_pipeline_terminate(pipeline);
                ESP_LOGD(TAG, "[ * ] pipeline reset ringbuffer");
                audio_pipeline_reset_ringbuffer(pipeline);
                ESP_LOGD(TAG, "[ * ] pipeline reset elements");
                audio_pipeline_reset_elements(pipeline); // optional? how many others should we reset? 
                ESP_LOGD(TAG, "[ * ] pipeline change state to init?");                
                audio_pipeline_change_state(pipeline, AEL_STATE_INIT);
                ESP_LOGD(TAG, "[ * ] pipeline run");
                audio_pipeline_run(pipeline);

                start_us = esp_timer_get_time(); 

                continue;
            }

            // fall through here so I can catch the unhandled event prints

        } 

        // grab button inputs (down only not release) and do things
        if ((msg.source_type == PERIPH_ID_TOUCH || msg.source_type == PERIPH_ID_BUTTON || msg.source_type == PERIPH_ID_ADC_BTN) &&
            (msg.cmd == PERIPH_TOUCH_TAP || msg.cmd == PERIPH_BUTTON_PRESSED || msg.cmd == PERIPH_ADC_BUTTON_PRESSED)) {
            if ((int) msg.data == get_input_volup_id()) {
                ESP_LOGI(TAG, "[ * ] [Vol+] touch tap event");
                player_volume += 10;
                if (player_volume > 100) {
                    player_volume = 100;
                }
                audio_hal_set_volume(board_handle->audio_hal, player_volume);
                ESP_LOGI(TAG, "[ * ] Volume set to %d %%", player_volume);
            } else if ((int) msg.data == get_input_voldown_id()) {
                ESP_LOGI(TAG, "[ * ] [Vol-] touch tap event");
                player_volume -= 10;
                if (player_volume < 0) {
                    player_volume = 0;
                }
                audio_hal_set_volume(board_handle->audio_hal, player_volume);
                ESP_LOGI(TAG, "[ * ] Volume set to %d %%", player_volume);
            }
            else {
                ESP_LOGI(TAG, "[ * ] Received periph unhandled event cmd %d data int %d",msg.cmd, (int) msg.data);
            }
            continue;
        }

        /* Stop when the last pipeline element (i2s_stream_writer in this case) receives stop event */
        if (msg.source_type == AUDIO_ELEMENT_TYPE_ELEMENT && msg.source == (void *) i2s_stream_writer
                && msg.cmd == AEL_MSG_CMD_REPORT_STATUS
                && (((int)msg.data == AEL_STATUS_STATE_STOPPED) || ((int)msg.data == AEL_STATUS_STATE_FINISHED))) {
            ESP_LOGW(TAG, "[ * ] I2S Stop event received (just reporting)");
            continue;
        }

// useful prints for understanding your data pipeline
#if 0
        // note: element is 1 << 17 is 131072 player is 1 << 18 service is 1 << 19 periph is 1 << 20
        if (msg.source == (void *)fatfs_stream_reader) {
            if (msg.cmd == AEL_MSG_CMD_REPORT_STATUS) 
                ESP_LOGI(TAG, "[ * ] Received unhandled fatfs source type %d status %s",msg.source_type, AEL_STATUS_STRINGS[(int) msg.data]);
            else
                ESP_LOGI(TAG, "[ * ] Received unhandled fatfs source type %d cmd %s data (int) %d",msg.source_type, AEL_COMMAND_STRINGS[msg.cmd], (int) msg.data);
        } else if (msg.source == (void *)*music_decoder) { 
            if (msg.cmd == AEL_MSG_CMD_REPORT_STATUS) 
                ESP_LOGI(TAG, "[ * ] Received unhandled decoder source type %d status %s",msg.source_type, AEL_STATUS_STRINGS[ (int) msg.data]);
            else
                ESP_LOGI(TAG, "[ * ] Received unhandled decoder source type %d cmd %s data (int) %d",msg.source_type, AEL_COMMAND_STRINGS[msg.cmd], (int) msg.data);
        } else if (msg.source == (void *)equalizer) { 
            if (msg.cmd == AEL_MSG_CMD_REPORT_STATUS) 
                ESP_LOGI(TAG, "[ * ] Received unhandled equalizer source type %d status %s",msg.source_type, AEL_STATUS_STRINGS[ (int) msg.data]);
            else
                ESP_LOGI(TAG, "[ * ] Received unhandled equalizer source type %d cmd %s data (int) %d",msg.source_type, AEL_COMMAND_STRINGS[msg.cmd], (int) msg.data);
        } else if (msg.source == (void *)i2s_stream_writer) { 
            if (msg.cmd == AEL_MSG_CMD_REPORT_STATUS) 
                ESP_LOGI(TAG, "[ * ] Received unhandled i2s source type %d status %s",msg.source_type, AEL_STATUS_STRINGS[ (int) msg.data]);
            else
                ESP_LOGI(TAG, "[ * ] Received unhandled i2s source type %d cmd %s data (int) %d",msg.source_type, AEL_COMMAND_STRINGS[msg.cmd], (int) msg.data);
        } else {
            ESP_LOGI(TAG, "[ * ] Received unhandled unknown source type %d cmd %d data (int) %d",msg.source_type, msg.cmd, (int) msg.data);
        }
#endif // useful debug

    }

    // the above loop is infinite so don't expect to get here :-P
    ESP_LOGI(TAG, "[ 7 ] Stop audio_pipeline");
    audio_pipeline_stop(pipeline);
    audio_pipeline_wait_for_stop(pipeline);
    audio_pipeline_terminate(pipeline);

    audio_pipeline_unregister(pipeline, fatfs_stream_reader);
    audio_pipeline_unregister(pipeline, i2s_stream_writer);
    audio_pipeline_unregister(pipeline, equalizer);
    audio_pipeline_unregister(pipeline, *music_decoder);

    /* Terminal the pipeline before removing the listener */
    audio_pipeline_remove_listener(pipeline);

    /* Stop all periph before removing the listener */
    esp_periph_set_stop_all(set);
    audio_event_iface_remove_listener(esp_periph_set_get_event_iface(set), evt);

    /* Make sure audio_pipeline_remove_listener & audio_event_iface_remove_listener are called before destroying event_iface */
    audio_event_iface_destroy(evt);

    /* Release all resources */
    audio_pipeline_deinit(pipeline);
    audio_element_deinit(fatfs_stream_reader);
    audio_element_deinit(i2s_stream_writer);
    audio_element_deinit(equalizer);
    audio_element_deinit(*music_decoder);
    esp_periph_set_destroy(set);
}
