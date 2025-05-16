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


static esp_err_t wav_header_read(wav_reader_state_t *state) {
    int fd = state->fd;
    
    if (lseek(fd, 0, SEEK_SET) < 0) {
        ESP_LOGE(TAG, "Failed to seek to beginning of file: %s", strerror(errno));
        return ESP_FAIL;
    }

    char chunk_id[5] = {0};
    uint32_t chunk_size;

    // Read RIFF chunk
    if (read(fd, chunk_id, 4) != 4 || strncmp(chunk_id, "RIFF", 4) != 0) {
        ESP_LOGE(TAG, "Invalid RIFF header");
        return ESP_FAIL;
    }

    if (read(fd, &chunk_size, 4) != 4) {
        ESP_LOGE(TAG, "Failed to read RIFF chunk size");
        return ESP_FAIL;
    }
    // We don't really use the RIFF chunk size, but we could check it against the file size

    if (read(fd, chunk_id, 4) != 4 || strncmp(chunk_id, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "Invalid WAVE header");
        return ESP_FAIL;
    }

    bool fmt_found = false;
    bool data_found = false;

    while (true) {
        if (read(fd, chunk_id, 4) != 4) {
            if (fmt_found && data_found) {
                ESP_LOGW(TAG, "End of file reached before all expected chunks were found.");
                break;  // It's not an error to reach EOF, but we should have fmt and data
            } else {
                ESP_LOGE(TAG, "Failed to read chunk ID or end of file reached prematurely");
                return ESP_FAIL;
            }
        }

        if (read(fd, &chunk_size, 4) != 4) {
            ESP_LOGE(TAG, "Failed to read chunk size");
            return ESP_FAIL;
        }

        if (strncmp(chunk_id, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                ESP_LOGE(TAG, "Invalid fmt chunk size: %" PRIu32, chunk_size);
                return ESP_FAIL;
            }

            if (read(fd, &state->audio_format, 2) != 2) {
                ESP_LOGE(TAG, "Failed to read audio format");
                return ESP_FAIL;
            }

            if (read(fd, &state->num_channels, 2) != 2) {
                ESP_LOGE(TAG, "Failed to read num_channels");
                return ESP_FAIL;        
            }
            if (read(fd, &state->sample_rate, 4) != 4) {
                ESP_LOGE(TAG, "Failed to read sample_rate");
                return ESP_FAIL;  
            }
            if (lseek(fd, 6, SEEK_CUR) < 0) {
                ESP_LOGE(TAG, "Failed to skip bytes per sec and block align %s", strerror(errno));
                return ESP_FAIL;  

            } // Skip bytes_per_sec and block_align
            
            if (read(fd, &state->bits_per_sample, 2) != 2) {
                ESP_LOGE(TAG, "Failed to read bits_per_sample");
                return ESP_FAIL;
            };

            state->block_align = state->num_channels * state->bits_per_sample / 8;
            state->bytes_per_sec = state->sample_rate * state->num_channels * state->bits_per_sample / 8;
            fmt_found = true;

            // Skip any remaining data in the fmt chunk if it's larger than the basic PCM info
            if (chunk_size > 16) {
                if (lseek(fd, chunk_size - 16, SEEK_CUR) < 0) {
                    ESP_LOGE(TAG, "Failed to seek past extra fmt chunk data: %s", strerror(errno));
                    return ESP_FAIL;
                }
            }
        } else if (strncmp(chunk_id, "data", 4) == 0) {
            state->data_size = chunk_size;
            state->data_offset = lseek(fd, 0, SEEK_CUR); // Record the offset
            data_found = true;
            break;  // Stop reading after the data chunk header
        } else {
            ESP_LOGW(TAG, "Skipping unknown chunk: %4s (size: %" PRIu32 ")", chunk_id, chunk_size);
            if (lseek(fd, (off_t)chunk_size, SEEK_CUR) < 0) {
                ESP_LOGE(TAG, "Failed to seek past unknown chunk: %s", strerror(errno));
                return ESP_FAIL;
            }
        }
    }

    if (!fmt_found || !data_found) {
        ESP_LOGE(TAG, "Required chunks (fmt and/or data) not found in WAV file");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "read wav header, found the following: ");
    ESP_LOGI(TAG, "audio_format: %d", (int) state->audio_format);
    ESP_LOGI(TAG, "num_channels: %d", (int) state->num_channels);
    ESP_LOGI(TAG, "sample_rate: %d", (int) state->sample_rate);
    ESP_LOGI(TAG, "bits_per_sample: %u", (unsigned int) state->bits_per_sample);
    ESP_LOGI(TAG, "data_size: %u", (unsigned int) state->data_size);
    ESP_LOGI(TAG, "block_align: %d", (int)state->block_align);
    ESP_LOGI(TAG, "data_offset: %jd", (intmax_t)state->data_offset);
    ESP_LOGI(TAG, "bytes_per_sec: %u", (unsigned int) state->bytes_per_sec);

    return ESP_OK;
}


/**
 * @brief Initialize the audio ring buffer in DMA memory.
 *
 * @return ESP_OK on success, ESP_FAIL on failure.
 */
static esp_err_t wav_reader_init_ringbuf( wav_reader_state_t *state ) {    

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
 * @brief Read audio data from file and copy to ring buffer.
 *
 * @param fd File descriptor.
 * @param header WAV header information.
 * @return This function does not return.
 */


static esp_err_t wav_read(wav_reader_state_t *state) {
    size_t bytes_read;
    size_t total_bytes_read = 0;
    esp_err_t err = ESP_OK;

    // Calculate initial offset within the first aligned block
    size_t current_read_size = WAV_READER_READ_SIZE - ( state->data_offset % WAV_READER_READ_SIZE );

    ESP_LOGD(TAG, "start: try read %zu bytes from file, offset %jd", current_read_size, (intmax_t) state->data_offset);

    // note about memory pool. Starting with MALLOC_CAP_INTERNAL, as I think I have enough space,
    // if there's not enough space, switching to MALLOC_CAP_SPIRAM is probably a good idea
    uint8_t* read_buffer = (uint8_t*)heap_caps_malloc(WAV_READER_READ_SIZE, MALLOC_CAP_INTERNAL);
    if (!read_buffer) {
        ESP_LOGE(TAG, "Failed to allocate read buffer");
        return ESP_FAIL;
    }

    // Seek to the beginning of the data
    if (lseek(state->fd, state->data_offset, SEEK_SET) < 0) {
        // FIXME: this is a bit of a footgun because two exit paths, thus
        // an easy way to write a memory leak. This is one of those rare cases
        // where a GOTO is better.
        ESP_LOGE(TAG, "Failed to seek to data offset: %s", strerror(errno));
        err = ESP_FAIL;
        goto cleanup;
    }

    while (total_bytes_read < state->data_size) {

        // catch the end case
        if (current_read_size > state->data_size - total_bytes_read) {
            current_read_size = state->data_size - total_bytes_read;
        }

        if (current_read_size > WAV_READER_READ_SIZE) {
            ESP_LOGE(TAG, "READ TOO MUCH OVERWRITE %zu should be max %zu",current_read_size,WAV_READER_READ_SIZE);
        }

        int64_t start_time = esp_timer_get_time();

        bytes_read = read(state->fd, read_buffer, current_read_size);
        if (bytes_read != current_read_size) {
            if (bytes_read == 0) {
                ESP_LOGI(TAG, "End of file reached while reading audio data");
                goto cleanup; // normal
            } else {
                ESP_LOGE(TAG, "Error reading from file: %s", strerror(errno));
                err = ESP_FAIL;
                goto cleanup; // Error
            }
        }
        int64_t delta = esp_timer_get_time() - start_time;
        if (delta > 4000) { // 1000 microseconds = 1 millisecond, adjust as needed
            ESP_LOGW(TAG, "Read operation took longer than expected: %lld us %zu bytes read", delta, bytes_read);
        }

        // ESP_LOGD(TAG, "read %zu bytes from file, writing to ringbuf %p", bytes_read, state->ringbuf);

        // Send data to ring buffer with infinite timeout
        // this should mean when the entire buffer goes in, the delay is released, and we can turn around
        // the next read, which becomes not polling but immediate. The ring buffer itself size should then be
        // sized bigger than 2x the sector read size, so this turns around. Problems could happen if 
        // there's something about the scheduler that delays the return from this call?

        start_time = esp_timer_get_time();

        BaseType_t result = xRingbufferSend(state->ringbuf, read_buffer, bytes_read, portMAX_DELAY);
        if (result != pdTRUE) {
            ESP_LOGE(TAG, "Failed to send data to ring buffer - probable timeout? - continuing");
            // Handle ring buffer full condition, potentially by waiting or retrying
        }

        // ok, if we are writing 4k, then we should have a combined speed of about 23ms. If the read time of the bytes
        // is about 4k, we should tot to about 18ms.
        delta = esp_timer_get_time() - start_time;
        if (delta > 40000) { // 1000 microseconds = 1 millisecond, adjust as needed
            ESP_LOGW(TAG, "RingBuffer Send operation took longer than expected: %lld us for %zu ", delta, bytes_read);
        }
        // expect to get control when there's still a fair amount of data, check if we're underflowing
        // although it's true after the first write because we're still filling it!
        size_t ringBufFreeSz = xRingbufferGetCurFreeSize(state->ringbuf);
        if ( WAV_READER_RINGBUF_SIZE - ringBufFreeSz < 4096 ) {
            ESP_LOGW(TAG, "RingBuffer full space smaller than expected after write: %zu bytes", WAV_READER_RINGBUF_SIZE - ringBufFreeSz);
        }

        total_bytes_read += bytes_read;
        current_read_size = WAV_READER_READ_SIZE;
    }

    ESP_LOGI(TAG, "Finished reading audio data. Total bytes read: %zu", total_bytes_read);
cleanup:
    free(read_buffer);
    ESP_LOGI(TAG, "wav_reader: returning with error %d",err);
    return err;
}

//
// init these shared components
// 

 esp_err_t wav_reader_init(wav_reader_state_t *state ) {

    int fd = -1;
    state->ringbuf = NULL;
    state->ringbuf_data_storage = NULL;
    state->ringbuf_struct_storage = NULL;

    if (wav_reader_init_ringbuf(state) != ESP_OK) {
        goto err;
    }

    state->fd = open(state->filepath, O_RDONLY);
    if (state->fd < 0) {
        ESP_LOGE(TAG, "Failed to open file: %s (%s)", state->filepath, strerror(errno));
        goto err;
    }

    if (wav_header_read(state) != ESP_OK) {
        goto err;
    }
    return ESP_OK;

err:
    ESP_LOGE(TAG, "Wav_reader_init failed ");
    if (state->fd >= 0)    close(fd);
    vRingbufferDelete(state->ringbuf);
    free(state->ringbuf_data_storage);
    free(state->ringbuf_struct_storage);
    return ESP_FAIL;
}

void wav_reader_deinit(wav_reader_state_t *state ) {

    ESP_LOGE(TAG, "Wav_reader deinit ");

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

void wav_reader_task(void* arg) {

    wav_reader_state_t * state = (wav_reader_state_t *)arg;
    esp_err_t err;

    do {

        ESP_LOGI(TAG, "task starting wav read");
        err = wav_read(state);
        ESP_LOGI(TAG, "TASK ending wav read");

    } while(err == ESP_OK);

    ESP_LOGE(TAG, "wav reader TASK:  exiting with error %d", err);
    state->done = true;

// haven't decided who will destructyfiy the state
    vTaskDelete(NULL);
}
