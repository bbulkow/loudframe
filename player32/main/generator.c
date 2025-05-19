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

#include "driver/i2s_std.h"

#include "player32.h"
#include "es8388.h"


// local

static const char *TAG = "sine_wave";

// static i2s_chan_handle_t i2s_tx_chan = NULL; // Handle to the TX channel

#define SAMPLE_RATE  44100

// Pins for the aithinker audio kit. Will refactor when it's working.


#define PI           3.14159265359

/**
 * @brief Generate and play a 16-bit stereo sine wave at 44.1 kHz without boundary clicks.
 *        The buffer size is chosen to be an integer multiple of the wave period.
 * 
 * Always transmits to the ES8388 driver
 *
 * @param frequency Desired frequency in Hz
 * @param amplitude Volume in [0.0, 1.0]
 */
void play_sine_wave(float frequency, float amplitude)
{

    ESP_LOGI(TAG,"Play sine wave: begin");

    // 1) Calculate how many samples constitute one period of the sine wave
    //    at 44.1 kHz. Then round to the nearest integer to avoid fractional samples.
    float exact_period = (float)SAMPLE_RATE / frequency;
    int period_samples = (int)(exact_period + 0.5f);
    if (period_samples <= 0) {
        ESP_LOGE(TAG,"Invalid period calculation");
        return;
    }

    // The actual frequency played = SAMPLE_RATE / period_samples
    // If this is not exactly 'frequency', there's a small pitch shift but no clicks.

    // 2) Each frame: 2 samples (left + right), int16_t each
    int total_stereo_samples = period_samples * 2;
    int total_bytes = total_stereo_samples * sizeof(int16_t);

    // 3) Allocate a buffer for one full cycle (stereo interleaved)
    int16_t *audio_buf = (int16_t *)malloc(total_bytes);
    if (!audio_buf) {
        ESP_LOGE(TAG,"Failed to allocate audio buffer");
        return;
    }

    // 4) Fill the buffer with one cycle of a sine wave
    float phase_increment = 2.0f * PI / (float)period_samples;
    float phase = 0.0f;

    for (int i = 0; i < period_samples; i++) {
        float sample_val = amplitude * sinf(phase) * 32767.0f;
        int16_t s = (int16_t)sample_val;

        audio_buf[2 * i]     = s;  // Left
        audio_buf[2 * i + 1] = s;  // Right

        phase += phase_increment;
        if (phase >= 2.0f * PI) {
            phase -= 2.0f * PI;
        }
    }

    // 5) Continuously play this buffer
    size_t tot_bytes_written = 0;
    // while (1) {
    //     // i2s_tx_chan was created in init_i2s_std()
    //     i2s_channel_write(i2s_tx_chan, audio_buf, total_bytes, &bytes_written, portMAX_DELAY);
    //     // In production code, handle any potential write errors or break conditions
    // }
    int kicker = 0;

    while (1) {

        size_t bytes_written = 0;
        esp_err_t ret = es8388_write(audio_buf, total_bytes, &bytes_written);
        tot_bytes_written += bytes_written;
        if (ret != ESP_OK) {
            ESP_LOGI(TAG, "play sine wave: returned error %d written %zu tot written %zu", ret, bytes_written, tot_bytes_written);
        }

        // this basically doesn't block, so call a yeild to update the watchdog and give other processes a chance
        // NOTE: kicking the watchdog doesn't seem to be helping.
        // only a delay with greater than 10ms seems to give us watchdog.
        taskYIELD();
        esp_task_wdt_reset();
        if (kicker++ % 30 == 0) { // every 30 loops do a long enough delay
            vTaskDelay(pdMS_TO_TICKS(11));
        }

    }

    // Not reached here in infinite loop
    free(audio_buf);
}