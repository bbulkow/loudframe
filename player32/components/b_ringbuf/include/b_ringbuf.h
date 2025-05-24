/*
 * MIT License
 *
 * (C) 2025 Brian Bulkowski brian@bulkowski.org
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __B_RINGBUF_H__
#define __B_RINGBUF_H__

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif


#define B_RINGBUF_ERROR_BASE    (0x700)
#define B_RINGBUF_ERR_ABORT     (B_RINGBUF_ERROR_BASE + 0x01)
#define B_RINGBUF_ERR_DONE     (B_RINGBUF_ERROR_BASE + 0x02)

typedef struct b_ringbuf *b_ringbuf_handle_t;


/**
 * @brief      Create ringbuffer with a given size and memory capabilities.
 *
 * @param[in]  size   Total size of the ringbuffer in bytes.
 * @param[in]  caps   Memory capabilities for buffer allocation (e.g., MALLOC_CAP_DMA).
 *
 * @return     ringbuf_handle_t
 */
b_ringbuf_handle_t brb_create(size_t size, uint32_t caps);

/**
 * @brief      Cleanup and free all memory created by ringbuf_handle_t
 *
 * @param[in]  rb    The Ringbuffer handle
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t brb_destroy(b_ringbuf_handle_t brb);

/**
 * @brief      Abort waiting until there is space for reading or writing of the ringbuffer
 *
 * @param[in]  rb    The Ringbuffer handle
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t brb_abort(b_ringbuf_handle_t brb);

/**
 * @brief      Reset ringbuffer, clear all values as initial state
 *
 * @param[in]  rb    The Ringbuffer handle
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t brb_reset(b_ringbuf_handle_t brb);

/**
 * @brief      Reset is_done_write flag
 *
 * @param[in]  rb    The Ringbuffer handle
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t brb_reset_is_done_write(b_ringbuf_handle_t brb);

/**
 * @brief      Get FREE bytes available of Ringbuffer
 *
 * @param[in]  rb    The Ringbuffer handle
 *
 * @return     total bytes available
 */
size_t brb_bytes_free(b_ringbuf_handle_t brb);

/**
 * @brief      Get the number of bytes that have filled the ringbuffer
 *
 * @param[in]  rb    The Ringbuffer handle
 *
 * @return     The number of bytes that have filled the ringbuffer
 */
size_t brb_bytes_filled(b_ringbuf_handle_t b_rb);

/**
 * @brief      Get total size of Ringbuffer (in bytes)
 *
 * @param[in]  rb    The Ringbuffer handle
 *
 * @return     total size of Ringbuffer
 */
size_t brb_get_size(b_ringbuf_handle_t b_rb);

/**
 * @brief      Read from Ringbuffer to `buf` with len and wait `tick_to_wait` ticks until enough bytes to read
 *             if the ringbuffer bytes available is less than `len`.
 *             If `buf` argument provided is `NULL`, then ringbuffer do pseudo reads by simply advancing pointers.
 *
 * @param[in]  rb             The Ringbuffer handle
 * @param      buf            The buffer pointer to read out data
 * @param[in]  len            The length request
 * @param[in]  ticks_to_wait  The ticks to wait
 *
 * @return     Number of bytes read
 */
esp_err_t brb_read(b_ringbuf_handle_t rb, uint8_t *buf, size_t *buf_len, TickType_t ticks_to_wait);

/**
** support zero copy. That requires the application doing their own copy, instead of providing a buffer.
*/

uint8_t *brb_receive_acquire(b_ringbuf_handle_t b_rb, size_t *pxSize, TickType_t ticks_to_wait, size_t max_size);

void brb_receive_complete(b_ringbuf_handle_t b_rb, void *data);

/**
 * @brief      Write to Ringbuffer from `buf` with `len` and wait `tick_to_wait` ticks until enough space to write
 *             if the ringbuffer space available is less than `len`
 *
 * @param[in]  rb             The Ringbuffer handle
 * @param      buf            The buffer
 * @param[in]  len            The length
 * @param[in]  ticks_to_wait  The ticks to wait
 *
 * @return     Number of bytes written
 */
esp_err_t brb_write(b_ringbuf_handle_t rb, uint8_t *buf, size_t *len, TickType_t ticks_to_wait);

/**
** support zero copy. That requires the application doing their own copy, instead of providing a buffer.
*/

void *brb_send_acquire(b_ringbuf_handle_t b_rb, size_t *pxSize, TickType_t ticks_to_wait, size_t max_size);

void brb_send_complete(b_ringbuf_handle_t b_rb, void *data);








/**
 * @brief      Set status of writing to ringbuffer is done
 *
 * @param[in]  rb    The Ringbuffer handle
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t brb_done_write(b_ringbuf_handle_t rb);

/**
 * @brief      Unblock from brb_read
 *
 * @param[in]  rb    The Ringbuffer handle
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t brb_unblock_reader(b_ringbuf_handle_t rb);

/**
 * @brief      Set the owner of the 'brb_read'.
 *
 * @param[in]  rb     The Ringbuffer handle
 * @param[in]  holder The owner of the 'brb_read'
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t brb_set_reader_holder(b_ringbuf_handle_t rb, void *holder);

/**
 * @brief      Get the owner of the 'brb_read'.
 *
 * @param[in]   rb     The Ringbuffer handle
 * @param[out]  holder The owner of the 'brb_read'
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t brb_get_reader_holder(b_ringbuf_handle_t rb, void **holder);

/**
 * @brief      Set the owner of the 'brb_write'.
 *
 * @param[in]  rb     The Ringbuffer handle
 * @param[in]  holder The owner of the 'brb_write'
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t brb_set_writer_holder(b_ringbuf_handle_t rb, void *holder);

/**
 * @brief      Get the owner of the 'brb_write'.
 *
 * @param[in]   rb     The Ringbuffer handle
 * @param[out]  holder The owner of the 'brb_write'
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t brb_get_writer_holder(b_ringbuf_handle_t rb, void **holder);

#ifdef __cplusplus
}
#endif

#endif /* __B_RINGBUF__ */
