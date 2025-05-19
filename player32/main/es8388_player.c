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
#include "freertos/ringbuf.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_task_wdt.h"

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


    ESP_LOGI(TAG, "ES8388 player startingw");

    while (!wav_state->done) {

        uint8_t *data = NULL;
        size_t bytes_read = 0;
        size_t bytes_written = 0;


        // Receive ALL data from the ring buffer - suspected incorrect because it'll not give the "double buffer"
        // effect we are hoping for
        // data = (uint8_t *)xRingbufferReceive(wav_state->ringbuf, &bytes_read, 0); // Non-blocking read
        // Trying this: read no more than X, which will get better slicing
        data = (uint8_t *)xRingbufferReceiveUpTo(wav_state->ringbuf, &bytes_read, 0, ES8388_PLAYER_WRITE_SIZE);
        if (data) {
            if (bytes_read > 0) {
                size_t total_written = 0;
                uint8_t *write_ptr = data;
                while (total_written < bytes_read) {
                    // Write the received data to the ES8388
                    ret = es8388_write(write_ptr, bytes_read - total_written, &bytes_written);
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "Error writing to ES8388: %s (exiting)", esp_err_to_name(ret));
                        break; // Exit the loop on error
                    }
                    if (bytes_written == 0) {
                        ESP_LOGE(TAG, "ES8388 write returned 0 bytes written but not complete, exiting");
                        break;
                    }

                    total_written += bytes_written;
                    write_ptr += bytes_written;
                }
            }
            // Return the item back to the ring buffer
            vRingbufferReturnItem(wav_state->ringbuf, (void *)data);
        } else {
            underflow_counter++;
            if ((underflow_counter % 10) == 0) {
                print_task_stats();
            }

            // No data available in the ring buffer
            // This can happen, so it's not necessarily an error.
            // You might want to add a small delay here to avoid busy-waiting.

            // vTaskDelay(1 / portTICK_PERIOD_MS);
            int64_t now = esp_timer_get_time() / 1000;
            ESP_LOGI(TAG, "UNDERFLOW %d glitchcount %lld",underflow_counter,now - glitch_time);
            glitch_time = now;

            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }

    ESP_LOGI(TAG, "ES8388 player exiting: total bytes written %zu",total_bytes_written);

    return ret;
}
