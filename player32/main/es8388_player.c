// Ready PLAYER32
//
// LOUDFRAME project. Attempt to do the world's simplest ESP32
// read a file from the sd card, and play it on the correct I2S device
// in a loop!

// Author: Brian Bulkowski <brian@bulkowski.org> (c) 2025

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <limits.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_task_wdt.h"

#include "b_ringbuf.h"
#include "player32.h"
#include "es8388.h"


// local

static const char *TAG = "es8388_player";

/**
 * @brief Plays a WAV file using the ES8388 audio codec.
 * @brief Plays a WAV file using the ES8388 audio codec. 
 *        This function reads audio data from a ring buffer and writes it to the ES8388 DAC.
 *
 * @pre The ES8388 codec must be initialized and started (DAC mode) before calling this function.
 * @pre The wav_state structure must be initialized, including the ring buffer containing the audio data.
 *      The ring buffer should be filled with audio data from a WAV file, typically by the wav_reader_task.
 *
 * @param wav_state Pointer to the wav_reader_state_t structure containing WAV file information and playback state.
 */


esp_err_t play_es8388_wav(wav_reader_state_t *wav_state) {
    esp_err_t ret = ESP_OK;
    size_t total_bytes_written = 0;
    int underflow_counter = 0;
    int64_t glitch_time = 0;
    uint8_t *data = 0;

    ESP_LOGI(TAG, "ES8388 player starting: done %d",(int)wav_state->done);

    // THis buffer has to be MALLOC_CAP_DMA because we're going to write to I2S
    data = heap_caps_malloc(ES8388_PLAYER_WRITE_SIZE, MALLOC_CAP_DMA);
    if (!data) {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return ESP_ERR_NO_MEM;
    };

    // precharge: wait until the ring buffer is full
    while (brb_bytes_free(wav_state->ringbuf) > 1024) {
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    while (!wav_state->done) {

        // Receive ALL data from the ring buffer - suspected incorrect because it'll not give the "double buffer"
        // effect we are hoping for
        // data = (uint8_t *)xRingbufferReceive(wav_state->ringbuf, &bytes_read, 0); // Non-blocking read
        // Trying this: read no more than X, which will get better slicing
        size_t data_len = ES8388_PLAYER_WRITE_SIZE;

        if (ESP_OK != brb_read(wav_state->ringbuf, data, &data_len, portMAX_DELAY)) {
            ESP_LOGW(TAG, "brb_read returned error");
        } 
        // else {
        //     ESP_LOGI(TAG, "brb_read: success size %zu",data_len);
        // }

        if (data_len > 0) {
            if (data_len != ES8388_PLAYER_WRITE_SIZE) {
                ESP_LOGW(TAG, "ES8388 did not take entire buffer: req %d got %d",ES8388_PLAYER_WRITE_SIZE,data_len);
            }

            size_t tot_written_len = 0;
            while (tot_written_len < data_len) {
                size_t bytes_written = 0;
                // Write the received data to the ES8388
                ret = es8388_write(data + tot_written_len, data_len - tot_written_len, &bytes_written);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Error writing to ES8388: %s (exiting)", esp_err_to_name(ret));
                    break; // Exit the loop on error
                }
                if (bytes_written == 0) {
                    ESP_LOGE(TAG, "ES8388 write returned 0 bytes written but not complete, exiting");
                    break;
                }

                tot_written_len += bytes_written;
            }
        }
        else {
            ESP_LOGW(TAG, "brb_read returned no bytes but also no error");

        }

    }

    ESP_LOGI(TAG, "ES8388 player exiting: total bytes written %zu",total_bytes_written);

    free(data);

    return ret;
}
