/*
 * MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
 * (c) 2025 Brian Bulkowski brian@bulkowski.org
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

// After creating my own audio streams using esp-idf ringbufs, which are FreeRTOS ringbufs,
// I am finding performance problems, and I see that ESP-ADF does not use the FreeRTOS ringbufs
// either. Yet, even though specifically made for audio, the espressif-made ringbufs lack key
// features, like the ability to zero-copy on read, the ability to choose the allocation memory.
// Therefore, I've started with theirs, but expect to add a great number of features.

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "errno.h"
#include "b_ringbuf.h"
#include "esp_log.h"

static const char *TAG = "B_BRINGBUF";

struct b_ringbuf {
    uint8_t *p_o;                   /**< Original pointer */
    uint8_t *volatile p_r;          /**< Read pointer */
    uint8_t *volatile p_w;          /**< Write pointer */
    volatile uint32_t fill_cnt;  /**< Number of filled bytes */
    uint32_t size;               /**< Buffer size */
    SemaphoreHandle_t can_read;
    SemaphoreHandle_t can_write;
    SemaphoreHandle_t lock;
    bool abort_read;
    bool abort_write;
    bool is_done_write;         /**< To signal that we are done writing */
    bool unblock_reader_flag;   /**< To unblock instantly from brb_read */
    void *reader_holder;
    void *writer_holder;
};

static esp_err_t brb_abort_read(b_ringbuf_handle_t b_rb);
static esp_err_t brb_abort_write(b_ringbuf_handle_t b_rb);
static void brb_release(SemaphoreHandle_t handle);

b_ringbuf_handle_t brb_create(size_t size, uint32_t caps)
{
    if (size < 4) {
        ESP_LOGE(TAG, "brb_create: Invalid size");
        return NULL;
    }

    b_ringbuf_handle_t b_rb;
    uint8_t *buf = NULL;

    b_rb = heap_caps_malloc(sizeof(struct b_ringbuf), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (NULL == b_rb)  {
        ESP_LOGE(TAG, "brb_create: structure malloc failed");
        return(NULL);
    }
    memset(b_rb, 0, sizeof(struct b_ringbuf));

    buf = heap_caps_malloc(size, caps);
    if (buf == NULL)  {
        ESP_LOGE(TAG, "brb_create:buffer malloc failed");
        goto _brb_create_failed;
    };

    b_rb->can_read   = xSemaphoreCreateBinary();
    b_rb->can_write  = xSemaphoreCreateBinary();
    b_rb->lock       = xSemaphoreCreateMutex();
    if (b_rb->can_read == NULL || b_rb->can_write == NULL || b_rb->lock == NULL)  {
        ESP_LOGE(TAG, "brb_create: a sem create failed");
        goto _brb_create_failed;
    }   

    b_rb->p_o = b_rb->p_r = b_rb->p_w = buf;
    b_rb->fill_cnt = 0;
    b_rb->size = size;
    b_rb->is_done_write = false;
    b_rb->unblock_reader_flag = false;
    b_rb->abort_read = false;
    b_rb->abort_write = false;
    return b_rb;

_brb_create_failed:
    brb_destroy(b_rb);
    return NULL;
}

esp_err_t brb_destroy(b_ringbuf_handle_t b_rb)
{
    if (b_rb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (b_rb->p_o) {
        free(b_rb->p_o);
        b_rb->p_o = NULL;
    }
    if (b_rb->can_read) {
        vSemaphoreDelete(b_rb->can_read);
        b_rb->can_read = NULL;
    }
    if (b_rb->can_write) {
        vSemaphoreDelete(b_rb->can_write);
        b_rb->can_write = NULL;
    }
    if (b_rb->lock) {
        vSemaphoreDelete(b_rb->lock);
        b_rb->lock = NULL;
    }
    free(b_rb);
    b_rb = NULL;
    return ESP_OK;
}

esp_err_t brb_reset(b_ringbuf_handle_t b_rb)
{
    if (b_rb == NULL) {
        return ESP_FAIL;
    }
    b_rb->p_r = b_rb->p_w = b_rb->p_o;
    b_rb->fill_cnt = 0;
    b_rb->is_done_write = false;

    b_rb->unblock_reader_flag = false;
    b_rb->abort_read = false;
    b_rb->abort_write = false;
    return ESP_OK;
}

esp_err_t brb_reset_is_done_write(b_ringbuf_handle_t b_rb)
{
    if (b_rb == NULL) {
        return ESP_FAIL;
    }
    b_rb->is_done_write = false;
    return ESP_OK;
}

size_t brb_bytes_free(b_ringbuf_handle_t b_rb)
{
    if (b_rb) {
        return (b_rb->size - b_rb->fill_cnt);
    }
    return ESP_FAIL;
}

size_t brb_bytes_filled(b_ringbuf_handle_t b_rb)
{
    if (b_rb) {
        return b_rb->fill_cnt;
    }
    return ESP_FAIL;
}

static void brb_release(SemaphoreHandle_t handle)
{
    xSemaphoreGive(handle);
}

#define brb_block(handle, time) xSemaphoreTake(handle, time)

esp_err_t brb_read(b_ringbuf_handle_t b_rb, uint8_t *buf, size_t *buf_len_r, TickType_t ticks_to_wait)
{
    size_t read_size = 0;
    size_t total_read_size = 0;
    size_t buf_len;
    esp_err_t ret_val = ESP_OK;

    if (b_rb == NULL || buf == NULL || buf_len_r == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    buf_len = *buf_len_r;

    while (buf_len) {
        //take buffer lock
        if (brb_block(b_rb->lock, portMAX_DELAY) != pdTRUE) {
            ret_val = ESP_ERR_TIMEOUT;
            goto read_err;
        }

        if (b_rb->fill_cnt < buf_len) {
            read_size = b_rb->fill_cnt;
            /**
             * When non-multiple of 4(word size) bytes are written to I2S, there is noise.
             * Below is the kind of workaround to read only in multiple of 4. Avoids noise when rb is read in small chunks.
             * Note that, when we have buf_len bytes available in rb, we still read those irrespective of if it's multiple of 4.
             */
            read_size = read_size & 0xfffffffc;
            if ((read_size == 0) && b_rb->is_done_write) {
                read_size = b_rb->fill_cnt;
            }
        } else {
            read_size = buf_len;
        }

        if (read_size == 0) {
            //no data to read, release thread block to allow other threads to write data

            if (b_rb->is_done_write) {
                ret_val = ESP_OK;
                brb_release(b_rb->lock);
                goto read_err;
            }
            if (b_rb->abort_read) {
                ret_val = B_RINGBUF_ERR_ABORT;
                brb_release(b_rb->lock);
                goto read_err;
            }
            if (b_rb->unblock_reader_flag) {
                //reader_unblock is nothing but forced timeout
                ret_val = ESP_ERR_TIMEOUT;
                brb_release(b_rb->lock);
                goto read_err;
            }

            brb_release(b_rb->lock);
            brb_release(b_rb->can_write);
            //wait till some data available to read
            if (brb_block(b_rb->can_read, ticks_to_wait) != pdTRUE) {
                ret_val = ESP_ERR_TIMEOUT;
                goto read_err;
            }
            continue;
        }

        if ((b_rb->p_r + read_size) > (b_rb->p_o + b_rb->size)) {
            int rlen1 = b_rb->p_o + b_rb->size - b_rb->p_r;
            int rlen2 = read_size - rlen1;
            if (buf) {
                memcpy(buf, b_rb->p_r, rlen1);
                memcpy(buf + rlen1, b_rb->p_o, rlen2);
            }
            b_rb->p_r = b_rb->p_o + rlen2;
        } else {
            if (buf) {
                memcpy(buf, b_rb->p_r, read_size);
            }
            b_rb->p_r = b_rb->p_r + read_size;
        }

        buf_len -= read_size;
        b_rb->fill_cnt -= read_size;
        total_read_size += read_size;
        buf += read_size;
        brb_release(b_rb->lock);
        if (buf_len == 0) {
            break;
        }
    }
read_err:
    if (total_read_size > 0) {
        brb_release(b_rb->can_write);
    }
    b_rb->unblock_reader_flag = false; /* We are anyway unblocking the reader */
    if (ret_val != ESP_OK) {
        *buf_len_r = 0;        
    }
    else {
        *buf_len_r = total_read_size;
    }
    return(ret_val);
}

esp_err_t brb_write(b_ringbuf_handle_t b_rb, uint8_t *buf, size_t *buf_len_r, TickType_t ticks_to_wait)
{
    size_t write_size;
    size_t total_write_size = 0;
    size_t buf_len;;
    esp_err_t ret_val = ESP_OK;

    if (b_rb == NULL || buf == NULL || buf_len_r == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    buf_len = *buf_len_r;

    while (buf_len) {
        //take buffer lock
        if (brb_block(b_rb->lock, portMAX_DELAY) != pdTRUE) {
            ret_val =  ESP_ERR_TIMEOUT;
            goto write_err;
        }
        write_size = brb_bytes_free(b_rb);

        if (buf_len < write_size) {
            write_size = buf_len;
        }

        if (write_size == 0) {
            //no space to write, release thread block to allow other to read data
            if (b_rb->is_done_write) {
                ret_val = B_RINGBUF_ERR_DONE;
                brb_release(b_rb->lock);
                goto write_err;
            }
            if (b_rb->abort_write) {
                ret_val = B_RINGBUF_ERR_ABORT;
                brb_release(b_rb->lock);
                goto write_err;
            }

            brb_release(b_rb->lock);
            brb_release(b_rb->can_read);
            //wait till we have some empty space to write
            if (brb_block(b_rb->can_write, ticks_to_wait) != pdTRUE) {
                ret_val = ESP_ERR_TIMEOUT;
                goto write_err;
            }
            continue;
        }

        if ((b_rb->p_w + write_size) > (b_rb->p_o + b_rb->size)) {
            int wlen1 = b_rb->p_o + b_rb->size - b_rb->p_w;
            int wlen2 = write_size - wlen1;
            memcpy(b_rb->p_w, buf, wlen1);
            memcpy(b_rb->p_o, buf + wlen1, wlen2);
            b_rb->p_w = b_rb->p_o + wlen2;
        } else {
            memcpy(b_rb->p_w, buf, write_size);
            b_rb->p_w = b_rb->p_w + write_size;
        }

        buf_len -= write_size;
        b_rb->fill_cnt += write_size;
        total_write_size += write_size;
        buf += write_size;
        brb_release(b_rb->lock);
        if (buf_len == 0) {
            break;
        }
    }
write_err:
    if (total_write_size > 0) {
        brb_release(b_rb->can_read);
    }
    if (ret_val == ESP_OK) {
        *buf_len_r = total_write_size;
    }
    else {
        *buf_len_r = 0;
    }
    return ret_val;
}

static esp_err_t brb_abort_read(b_ringbuf_handle_t b_rb)
{
    if (b_rb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    b_rb->abort_read = true;
    xSemaphoreGive(b_rb->can_read);
    return ESP_OK;
}

static esp_err_t brb_abort_write(b_ringbuf_handle_t b_rb)
{
    if (b_rb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    b_rb->abort_write = true;
    xSemaphoreGive(b_rb->can_write);
    return ESP_OK;
}

esp_err_t brb_abort(b_ringbuf_handle_t b_rb)
{
    if (b_rb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = brb_abort_read(b_rb);
    err |= brb_abort_write(b_rb);
    return err;
}

bool brb_is_full(b_ringbuf_handle_t b_rb)
{
    if (b_rb == NULL) {
        return false;
    }
    return (b_rb->size == b_rb->fill_cnt);
}

esp_err_t brb_done_write(b_ringbuf_handle_t b_rb)
{
    if (b_rb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    b_rb->is_done_write = true;
    brb_release(b_rb->can_read);
    return ESP_OK;
}

esp_err_t brb_unblock_reader(b_ringbuf_handle_t b_rb)
{
    if (b_rb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    b_rb->unblock_reader_flag = true;
    brb_release(b_rb->can_read);
    return ESP_OK;
}

bool brb_is_done_write(b_ringbuf_handle_t b_rb)
{
    if (b_rb == NULL) {
        return false;
    }
    return (b_rb->is_done_write);
}

size_t brb_get_size(b_ringbuf_handle_t b_rb)
{
    if (b_rb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return b_rb->size;
}

esp_err_t brb_set_reader_holder(b_ringbuf_handle_t b_rb, void *holder)
{
    if (b_rb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    b_rb->reader_holder = holder;
    return ESP_OK;
}

esp_err_t brb_get_reader_holder(b_ringbuf_handle_t b_rb, void **holder)
{
    if ((b_rb==NULL) || (holder == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    *holder = b_rb->reader_holder;
    return ESP_OK;
}

esp_err_t brb_set_writer_holder(b_ringbuf_handle_t b_rb, void *holder)
{
    if (b_rb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    b_rb->writer_holder = holder;
    return ESP_OK;
}

esp_err_t brb_get_writer_holder(b_ringbuf_handle_t b_rb, void **holder)
{
    if ((b_rb==NULL) || (holder == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    *holder = b_rb->writer_holder;
    return ESP_OK;
}