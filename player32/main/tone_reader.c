// wavPlayer
//
// LOUDFRAME project. Attempt to do the world's simplest ESP32
// read a file from the sd card, and play it on the correct I2S device
// in a loop!

// Author: Brian Bulkowski <brian@bulkowski.org> (c) 2025
// Assistance from Gemini Code Assistant

#include <stdio.h>
#include <stdlib.h> 
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <stdint.h>
#include <errno.h>
#include <inttypes.h>


#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/ringbuf.h"
#include "esp_heap_caps.h"

#include "esp_timer.h"
#include "esp_log.h"

#include "player32.h"

#define TAG "wavReader"


// Function that takes a pointer to a file, reads from that file the wav header, populates the structure
// that includes key information, and positions the file at the beginning of the actual audio data

/**
 * @brief Read WAV file header
 * 
 * Reads and parses the WAV file header into the header structure.
 * 
 * I would normally attempt to reduce the number of system calls. There are a lot of cases
 * where 3 calls can be reduced to 1. We could keep the current size as a variable, instead
 * of calling lseek frequently to find the current position. But let's face it: reading the
 * header is not performance critical and these are a pretty small number of calls. Reducing
 * the overall system overhead can be done if optimization is required
 * 
 * @param fd File descriptor
 * @param header Pointer to header structure to fill
 * @return ESP_OK on successful read
 *         ESP_FAIL if read fails
 */

// This code only works on little endian, which is all the esp-idf code
#if defined(__BIG_ENDIAN__)
    BOOGER BOOGER
#endif



/**
 * @brief Initialize the audio ring buffer in DMA memory.
 *
 * @return ESP_OK on success, ESP_FAIL on failure.
 */
static esp_err_t tone_reader_init_ringbuf( wav_reader_state_t *state ) {    

    ESP_LOGI(TAG, "initalizing ringbuf");

    state->ringbuf_data_storage = (uint8_t*)heap_caps_malloc(WAV_READER_RINGBUF_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!state->ringbuf_data_storage) {
        ESP_LOGE(TAG, "Failed to allocate ring buffer data storage");
        return ESP_FAIL;
    }

    // NB: 8 bit is required, as there are Testandset, in the structure part. 
    // If you just do internal you seem to get something that doesn't work.
    state->ringbuf_struct_storage = (StaticRingbuffer_t *) heap_caps_malloc(sizeof(StaticRingbuffer_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT );
    if (!state->ringbuf_struct_storage) {
        ESP_LOGE(TAG, "Failed to allocate ring buffer struct storage");
        free(state->ringbuf_data_storage);
        state->ringbuf_data_storage = 0;
        return ESP_FAIL;
    }

    RingbufHandle_t rb = xRingbufferCreateStatic(WAV_READER_RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF, state->ringbuf_data_storage, state->ringbuf_struct_storage);
    if (!rb) {
        ESP_LOGE(TAG, "Failed to create ring buffer");
        free(state->ringbuf_data_storage); // Free the allocated storage on failure
        state->ringbuf_data_storage = 0;
        free(state->ringbuf_struct_storage); // Free the allocated storage on failure
        state->ringbuf_struct_storage = 0;
        return ESP_FAIL;
    }

    state->ringbuf = rb;

    return ESP_OK;
}

/**
 * @brief Generate a tone of the same sample speed and inject, for test pruposes
 *
 * @param fd File descriptor.
 * @param header WAV header information.
 * @return This function does not return.
 */


#define PI           3.14159265359


static esp_err_t tone_reader_generate(float frequency, float amplitude, wav_reader_state_t *state) {

    size_t total_bytes_read = 0;
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG,"Tone generator sine wave: begin");

    // 1) Calculate how many samples constitute one period of the sine wave
    //    at 44.1 kHz. Then round to the nearest integer to avoid fractional samples.
    float exact_period = (float)state->bytes_per_sec / frequency;
    int period_samples = (int)(exact_period + 0.5f);
    if (period_samples <= 0) {
        ESP_LOGE(TAG,"Invalid period calculation");
        return(ESP_FAIL);
    }

    // The actual frequency played = SAMPLE_RATE / period_samples
    // If this is not exactly 'frequency', there's a small pitch shift but no clicks.

    // 2) Each frame: 2 samples (left + right), int16_t each
    int total_stereo_samples = period_samples * 2;
    int tone_len = total_stereo_samples * sizeof(int16_t);

    // 3) Allocate a buffer for one full cycle (stereo interleaved)
    int16_t *tone_buf = (int16_t *)malloc(tone_len);
    if (!tone_buf) {
        ESP_LOGE(TAG,"Failed to allocate audio buffer");
        return(ESP_FAIL);
    }

    // 4) Fill the buffer with one cycle of a sine wave
    float phase_increment = 2.0f * PI / (float)period_samples;
    float phase = 0.0f;

    for (int i = 0; i < period_samples; i++) {
        float sample_val = amplitude * sinf(phase) * 32767.0f;
        int16_t s = (int16_t)sample_val;

        tone_buf[2 * i]     = s;  // Left
        tone_buf[2 * i + 1] = s;  // Right

        phase += phase_increment;
        if (phase >= 2.0f * PI) {
            phase -= 2.0f * PI;
        }
    }

    while (total_bytes_read < state->data_size) {
        // ESP_LOGD(TAG, "read %zu bytes from file, writing to ringbuf %p", bytes_read, state->ringbuf);

        // Send data to ring buffer with infinite timeout
        // this should mean when the entire buffer goes in, the delay is released, and we can turn around
        // the next read, which becomes not polling but immediate. The ring buffer itself size should then be
        // sized bigger than 2x the sector read size, so this turns around. Problems could happen if 
        // there's something about the scheduler that delays the return from this call?

        uint64_t start_time = esp_timer_get_time();

        BaseType_t result = xRingbufferSend(state->ringbuf, tone_buf, tone_len, portMAX_DELAY);
        if (result != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send data to ring buffer - probable timeout? - continuing");
            // Handle ring buffer full condition, potentially by waiting or retrying
        }

        // ok, if we are writing 4k, then we should have a combined speed of about 23ms. If the read time of the bytes
        // is about 4k, we should tot to about 18ms.
        uint64_t delta = esp_timer_get_time() - start_time;
        if (delta > (100 * 1000) ) { // 1000 microseconds = 1 millisecond, adjust as needed
            ESP_LOGW(TAG, "RingBuffer Send operation took longer than expected: %lld us for %zu ", delta, tone_len);
        }
        // expect to get control when there's still a fair amount of data, check if we're underflowing
        // although it's true after the first write because we're still filling it!
        size_t ringBufFreeSz = xRingbufferGetCurFreeSize(state->ringbuf);
        if ( WAV_READER_RINGBUF_SIZE - ringBufFreeSz < 4096 ) {
            ESP_LOGW(TAG, "RingBuffer full space smaller than expected after write: %zu bytes", WAV_READER_RINGBUF_SIZE - ringBufFreeSz);
        }

        total_bytes_read += tone_len;
    } while (1);

    ESP_LOGI(TAG, "Finished reading audio data. Total bytes read: %zu", total_bytes_read);

// cleanup:
    free(tone_buf);
    ESP_LOGI(TAG, "tone_reader: returning with error %d",err);
    return err;
}

//
// init these shared components
// 

 esp_err_t tone_reader_init(wav_reader_state_t *state ) {

    int fd = -1;
    state->ringbuf = NULL;
    state->ringbuf_data_storage = NULL;
    state->ringbuf_struct_storage = NULL;

    if (tone_reader_init_ringbuf(state) != ESP_OK) {
        goto err;
    }

    state->fd = open(state->filepath, O_RDONLY);
    if (state->fd < 0) {
        ESP_LOGE(TAG, "Failed to open file: %s (%s)", state->filepath, strerror(errno));
        goto err;
    }

    // seems odd, but we are emulating the wav system, so we will still read the same wav header
    // and generate the tone just as if it was the same frequency etc
    if (wav_reader_header_read(state) != ESP_OK) {
        goto err;
    }
    return ESP_OK;

err:
    ESP_LOGE(TAG, "reader_init failed ");
    if (state->fd >= 0)    close(fd);
    vRingbufferDelete(state->ringbuf);
    free(state->ringbuf_data_storage);
    free(state->ringbuf_struct_storage);
    return ESP_FAIL;
}

void tone_reader_deinit(wav_reader_state_t *state ) {

    ESP_LOGE(TAG, "deinit ");

    if (state->fd >= 0)    close(state->fd);
    vRingbufferDelete(state->ringbuf);
    free(state->ringbuf_data_storage);
    free(state->ringbuf_struct_storage);
    if (state != NULL)    memset(state,0xff, sizeof(wav_reader_state_t));
    free(state);
    return;
}

/**
 * @brief Wrapper function for audio processing task.
 *
 * Initializes the ring buffer, reads the WAV header, and starts the
 * audio reading and buffering loop.
 *
 * @param arg File path to the WAV file (passed as void*).
 */

void tone_reader_task(void* arg) {

    wav_reader_state_t * state = (wav_reader_state_t *)arg;
    esp_err_t err;

    do {

        ESP_LOGI(TAG, "task starting tone read");
        err = tone_reader_generate(440.0, 0.5, state);
        ESP_LOGI(TAG, "TASK ending tone read");

    } while(err == ESP_OK);

    ESP_LOGE(TAG, "wav reader TASK:  exiting with error %d", err);
    state->done = true;

// haven't decided who will destructyfiy the state
    vTaskDelete(NULL);
}
