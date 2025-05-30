/* Code for Maxbotix ultrasonic sensor */

// originally taken by brian@bulkowski.org from 
// https://github.com/apalrd/PalHA_SaltSensor.git

// Bought a MaxBotics EZ1 from Adafruit.
// Should be an ultrasonic sensor with about the right distance and scope to detect humans


#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "maxbotics.h"

static const char *TAG = "maxDriver";


/* Internal uart data */
#define BUF_SIZE (1024)
static QueueHandle_t uart_queue;

/* Return data buffer in centimeters integer */
static volatile uint16_t samples[MAXBOTIX_SAMPLE_BUFFER_SIZE];
static volatile int16_t sample_next = 0; 
static volatile int16_t sample_count = 0;
static volatile TickType_t sample_last = 0;

/* Semaphore to lock sample data */
SemaphoreHandle_t sample_lock = NULL;

/* Task to handle maxbotics events */
static void maxbotix_event_handler(void *pvParameters)
{
    uart_event_t event;
    size_t buffered_size;
    static uint8_t dtmp[BUF_SIZE];

    ESP_LOGI(TAG,"Starting UART read task");

    for(;;) {
        //Waiting for UART event.

        ESP_LOGD(TAG,"blocking on UART queue read");

        if(xQueueReceive(uart_queue, (void * )&event, (TickType_t)portMAX_DELAY)) {

            ESP_LOGD(TAG, "UART read: event type %d", event.type);

            switch(event.type) {
                //Event of UART receving data
                /*We'd better handler data event fast, there would be much more data events than
                other types of events. If we take too much time on data event, the queue might
                be full.*/
                case UART_DATA:
                    ESP_LOGW(TAG,"UART Data received %d, Flushing", event.size);
                    uart_flush_input(MAXBOTIX_UART_NUM);
                    break;
                //Event of HW FIFO overflow detected
                case UART_FIFO_OVF:
                    ESP_LOGE(TAG, "UART HW FIFO Overflow");
                    // If fifo overflow happened, you should consider adding flow control for your application.
                    // The ISR has already reset the rx FIFO,
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(MAXBOTIX_UART_NUM);
                    xQueueReset(uart_queue);
                    break;
                //Event of UART ring buffer full
                case UART_BUFFER_FULL:
                    ESP_LOGE(TAG, "UART Rung Buffer Full");
                    // If buffer full happened, you should consider encreasing your buffer size
                    // As an example, we directly flush the rx buffer here in order to read more data.
                    uart_flush_input(MAXBOTIX_UART_NUM);
                    xQueueReset(uart_queue);
                    break;
                //Event of UART RX break detected
                case UART_BREAK:
                    ESP_LOGI(TAG, "UART RX Break");
                    break;
                //Event of UART parity check error
                case UART_PARITY_ERR:
                    ESP_LOGW(TAG, "UART Parity Error");
                    break;
                //Event of UART frame error
                case UART_FRAME_ERR:
                    ESP_LOGW(TAG, "UART Frame Error");
                    break;
                //UART_PATTERN_DET
                case UART_PATTERN_DET:
                    uart_get_buffered_data_len(MAXBOTIX_UART_NUM, &buffered_size);
                    int pos = uart_pattern_pop_pos(MAXBOTIX_UART_NUM);
                    ESP_LOGD(TAG, "[UART PATTERN DETECTED] pos: %d, buffered size: %d", pos, buffered_size);
                    if (pos == -1) {
                        // There used to be a UART_PATTERN_DET event, but the pattern position queue is full so that it can not
                        // record the position. We should set a larger queue size.
                        // As an example, we directly flush the rx buffer here.
                        ESP_LOGE(TAG,"UART Pattern Buffer Full, flushing");
                        uart_flush_input(MAXBOTIX_UART_NUM);
                    } else {
                        uart_read_bytes(MAXBOTIX_UART_NUM, dtmp, pos+1, 100 / portTICK_PERIOD_MS);
                        ESP_LOGD(TAG, "read data: %s", dtmp);
                        int32_t temp = 0;
                        uint32_t ret = sscanf((char*)dtmp,"R%"PRId32,&temp);
                        ESP_LOGD(TAG,"read integer %" PRId32 ", code %" PRIu32 ,temp,ret);
                        /* If ret is 1, insert sample */
                        if(ret)
                        {
                            /* Lock the semaphore for buffer access */
                            xSemaphoreTake(sample_lock,portMAX_DELAY);

                            /* Calculate time since last sample, store last time */
                            TickType_t now = xTaskGetTickCount();
                            TickType_t delta = now - sample_last;
                            TickType_t last = sample_last;
                            sample_last = now;
                            int32_t delta_ms = pdTICKS_TO_MS(delta);

                            /* If sample is too old, flush array */
                            if(delta_ms > MAXBOTIX_SAMPLE_AGE_MAX)
                            {
                                /* Reset buffer count and sample next to zero */
                                sample_next = 0;
                                sample_count = 0;
                            }

                            /* Insert value in array */
                            samples[sample_next] = temp;

                            /* Increment and wrap sample next index into circular buffer */
                            sample_next++;
                            if(sample_next >= MAXBOTIX_SAMPLE_BUFFER_SIZE)
                            {
                                sample_next = 0;
                            }

                            /* Increment and clamp sample count in buffer */
                            if(sample_count < MAXBOTIX_SAMPLE_BUFFER_SIZE)
                            {
                                sample_count++;
                            }

                            /* Complete semaphore action */
                            xSemaphoreGive(sample_lock);

                            /* Talk about what we did */
                            ESP_LOGI(TAG,"received sample value %" PRId32 ", inserting at index %" PRId16 ,temp,sample_next);
                            ESP_LOGD(TAG,"receive time is %" PRIu32 ", last was %" PRIu32 ", delta %" PRIu32,now,last,delta_ms);
                        }
                        else
                        {
                            ESP_LOGW(TAG,"read invalid response from sensor, got %s",dtmp);
                        }
                    }
                    break;
                //Others
                default:
                    ESP_LOGI(TAG, "uart unknown event type: %d", event.type);
                    break;
            }
        }
    }
    ESP_LOGI(TAG, "uart ending reader task");
    vTaskDelete(NULL);
}

void maxbotix_init(void)
{
    // note: if you want debug, make sure you set the min log level in MENUCONFIG to debug too
        // note: if you want debug, make sure you set the min log level in MENUCONFIG to debug too
    esp_log_level_set(TAG, ESP_LOG_WARN);
    // esp_log_level_set(TAG, ESP_LOG_DEBUG);

    gpio_set_pull_mode(MAXBOTIX_UART_NUM, GPIO_PULLUP_ONLY);

    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    //Install UART driver, and get the queue.
    int intr_alloc_flags = 0;
#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif
    uart_driver_install(MAXBOTIX_UART_NUM, BUF_SIZE * 4, BUF_SIZE, 50, &uart_queue, intr_alloc_flags);
    ESP_ERROR_CHECK( uart_param_config(MAXBOTIX_UART_NUM, &uart_config) );

    //Set UART pins (using UART0 default pins ie no changes.)
    ESP_ERROR_CHECK( uart_set_pin(MAXBOTIX_UART_NUM, UART_PIN_NO_CHANGE, MAXBOTIX_UART_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) );

    // Not sure why this is required for the maxbotix. Maybe it was just due to how the salt sensor thing was wired?
    ESP_ERROR_CHECK( uart_set_line_inverse(MAXBOTIX_UART_NUM,UART_SIGNAL_RXD_INV) );

    /* Set uart pattern detect function to detect a single carriage return */
    ESP_ERROR_CHECK( uart_enable_pattern_det_baud_intr(MAXBOTIX_UART_NUM, 0x0d, 1, 9, 0, 0) );
    //Reset the pattern queue length to record at most 20 pattern positions.
    ESP_ERROR_CHECK( uart_pattern_queue_reset(MAXBOTIX_UART_NUM, 20) );

    /* Create a semaphore for sample data */
    sample_lock = xSemaphoreCreateMutex();

    if(sample_lock == NULL)
    {
        ESP_LOGE(TAG,"Failed to create semaphore!");
        abort();
    }

    /* Reset sample data and count */
    sample_count = 0;
    sample_next = 0;

    /* Reset sample time to be at least age max ago so it doens't think data is new */
    sample_last = xTaskGetTickCount() - pdMS_TO_TICKS((MAXBOTIX_SAMPLE_AGE_MAX + 1));

    /* Free semaphore */
    xSemaphoreGive(sample_lock);

    /* Create a task to handler UART event from ISR */
    // BB - MAX_PRIORITIES-1 is better
    xTaskCreatePinnedToCore(maxbotix_event_handler, "maxbotix_eh", 1024 * 4, NULL, configMAX_PRIORITIES -1, NULL,1);
}

/* Routine to return the latest sample */
uint16_t maxbotix_get_latest(void)
{
    uint16_t RetVal = 65535;
    /* Take mutex */
    xSemaphoreTake(sample_lock,portMAX_DELAY);

    /* Check if there is at least one sample */
    if(sample_count)
    {
        int16_t idx = sample_next - 1;
        if(sample_next < 0) sample_next = (MAXBOTIX_SAMPLE_BUFFER_SIZE - 1);
        RetVal = samples[idx];
    }
    xSemaphoreGive(sample_lock);

    return RetVal;
}

int32_t maxbotix_get_age(void)
{
    /* If data is too old, return -1 */
    TickType_t now = xTaskGetTickCount();
    TickType_t delta = now - sample_last;
    int32_t delta_ms = pdTICKS_TO_MS(delta);
    return delta_ms;
}

void print_samples(uint16_t *arr,int16_t count)
{
    /* Print the whole array at once */
    static char my_printbuf[MAXBOTIX_SAMPLE_BUFFER_SIZE * 8];
    my_printbuf[0] = 0;
    for(int i = 0; i < count;i++)
    {
        char temp[8];
        sprintf(temp,"%04x ",(int)arr[i]);
        strcat(my_printbuf,temp);
    }
    ESP_LOGI(TAG,"buffer is %s",my_printbuf);
}


float maxbotix_get_median(float pct,int16_t min_count,int16_t max_count,int16_t *act_count)
{
    /* My copy of the data */
    static uint16_t my_samples_raw[MAXBOTIX_SAMPLE_BUFFER_SIZE];
    static uint16_t my_samples_sel[MAXBOTIX_SAMPLE_BUFFER_SIZE];
    static int16_t my_sample_next = 0; 
    static int16_t my_sample_count = 0;
    static TickType_t my_sample_last = 0;

    /* Copy the data in a protected region */
    xSemaphoreTake(sample_lock,portMAX_DELAY);

    /* Copy all data to our clone buffers */
    memcpy(my_samples_raw,(void *)samples,MAXBOTIX_SAMPLE_BUFFER_SIZE*sizeof(uint16_t));
    my_sample_next = sample_next;
    my_sample_count = sample_count;
    my_sample_last = sample_last;

    /* End protected region */
    xSemaphoreGive(sample_lock);

    ESP_LOGD(TAG,"Sample buffer copied from interrupt (count %d, next %d)",my_sample_count,my_sample_next);
    //print_samples(my_samples_raw,my_sample_count);

    /* If data is too old, return -1 */
    TickType_t now = xTaskGetTickCount();
    TickType_t delta = now - my_sample_last;
    int32_t delta_ms = pdTICKS_TO_MS(delta);
    if(delta_ms >= MAXBOTIX_SAMPLE_AGE_MAX)
    {
        ESP_LOGW(TAG,"Samples too old to perform median filter");
        return -1.0f;
    }

    /* If min count is less than 1, return -1 */
    if(min_count < 1)
    {
        ESP_LOGW(TAG,"Invalid minimum count to perform median filter");
        return -2.0f;
    }

    /* If percent is out of range, return -1 */
    if(pct < 0.0f || pct > 1.0f)
    {
        ESP_LOGW(TAG,"Invalid percentage to perform median filter");
        return -3.0f;
    }

    /* If below min count, return -1 */
    if(my_sample_count < min_count)
    {
        ESP_LOGW(TAG,"Not enough samples to perform median filter");
        return -4.0f;
    }

    /* Copy the lower of max_count or actual sample count into the selected array */
    int16_t act_samp = (max_count > my_sample_count) ? my_sample_count : max_count;
    for(int i = 0; i < act_samp; i++)
    {
        /* Retrieve from latest index in samples and go backward */
        int16_t idx = my_sample_next - i - 1;
        if(idx < 0) idx += MAXBOTIX_SAMPLE_BUFFER_SIZE;
        my_samples_sel[i] = my_samples_raw[idx];
    }

    /* Return actual samples if pointer is non null */
    if(act_count != NULL)
    {
        act_count[0] = act_samp;
    }


    ESP_LOGD(TAG,"Sample buffer from selecting back samples, %d samples",act_samp);
    //print_samples(my_samples_sel,act_samp);

    /* Perform insertion sort on array */
    for(int i = 1; i < act_samp;i++)
    {
        uint16_t key = my_samples_sel[i];
        int j = i - 1;

        /* Move elements of arr[0..i-1], that are
         * greater than key, to one position ahead
         * of their current position */
        while (j >= 0 && my_samples_sel[j] > key)
        {
            my_samples_sel[j + 1] = my_samples_sel[j];
            j = j - 1;
        }
        my_samples_sel[j + 1] = key;
    }


    ESP_LOGD(TAG,"Sample buffer from sorting samples");
    //print_samples(my_samples_sel,act_samp);

    /* Take the mean of the middle region, based on pct */
    int32_t pct_samples = act_samp * pct;
    ESP_LOGI(TAG,"Samples to be included in percentage filter: %"PRId32" (of %"PRIu16" total)",pct_samples,act_samp);
    pct_samples = (pct_samples + 1) >> 1;
    float mean = 0;
    for(int i = pct_samples; i < (act_samp - pct_samples); i++)
    {
        mean += (float)my_samples_sel[i];
    }
    mean = mean / (float)(act_samp - pct_samples - pct_samples);

    ESP_LOGI(TAG,"Actual mean calculated as %f",(double)mean);

    return mean;

}