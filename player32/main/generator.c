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

#include "driver/i2s_std.h"

#include "player32.h"

// local
//#include "es8388.h"

static const char *TAG = "sine_wave";

static i2s_chan_handle_t i2s_tx_chan = NULL; // Handle to the TX channel

#define SAMPLE_RATE  44100

// Pins for the aithinker audio kit. Will refactor when it's working.

#define PIN_I2S_MCLK   GPIO_NUM_0  // Replace x with your MCLK pin
#define PIN_I2S_BCLK   GPIO_NUM_27  // Replace y with your BCLK pin
#define PIN_I2S_WS     GPIO_NUM_25  // Replace z with your WS (LRCLK) pin
#define PIN_I2S_DOUT   GPIO_NUM_26  // Replace w with your data-out pin
#define PIN_I2S_DIN   GPIO_NUM_35  // Replace w with your data-out pin


/**
 * @brief Initialize the standard I2S driver (TX only) for 44.1kHz, 16-bit stereo
 */
esp_err_t init_i2s_std(void)
{
    // 1) Configure a single channel in Master role
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    // Optionally override defaults:
    // chan_cfg.dma_desc_num = 8;
    // chan_cfg.dma_frame_num = 240;

    // 2) Create the channel: TX handle assigned, RX is NULL for TX-only
    ESP_RETURN_ON_ERROR(
        i2s_new_channel(&chan_cfg, &i2s_tx_chan, NULL),
        TAG,
        "Failed to allocate I2S channel"
    );

    // 3) Create a standard I2S configuration
    i2s_std_config_t std_cfg = {
        // Clock config for 44.1 kHz
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        // 16 bits per sample, stereo
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        // Pin configuration
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,  // Not receiving
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    // 4) Initialize the channel with the standard I2S configuration
    ESP_RETURN_ON_ERROR(
        i2s_channel_init_std_mode(i2s_tx_chan, &std_cfg),
        TAG,
        "Failed to init I2S channel"
    );

    // 5) Enable the channel before using it
    ESP_RETURN_ON_ERROR(
        i2s_channel_enable(i2s_tx_chan),
        TAG,
        "Failed to enable I2S channel"
    );

    return ESP_OK;
}


#define PI           3.14159265359

/**
 * @brief Generate and play a 16-bit stereo sine wave at 44.1 kHz without boundary clicks.
 *        The buffer size is chosen to be an integer multiple of the wave period.
 *
 * @param frequency Desired frequency in Hz
 * @param amplitude Volume in [0.0, 1.0]
 */
void play_sine_wave(float frequency, float amplitude)
{
    // 1) Calculate how many samples constitute one period of the sine wave
    //    at 44.1 kHz. Then round to the nearest integer to avoid fractional samples.
    float exact_period = (float)SAMPLE_RATE / frequency;
    int period_samples = (int)(exact_period + 0.5f);
    if (period_samples <= 0) {
        printf("Invalid period calculation\n");
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
        printf("Failed to allocate audio buffer\n");
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
    size_t bytes_written = 0;
    while (1) {
        // i2s_tx_chan was created in init_i2s_std()
        i2s_channel_write(i2s_tx_chan, audio_buf, total_bytes, &bytes_written, portMAX_DELAY);
        // In production code, handle any potential write errors or break conditions
    }

    // Not reached here in infinite loop
    free(audio_buf);
}