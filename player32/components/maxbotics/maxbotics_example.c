#include <stdio.h>

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"

#include "max_driver.h"

static const char *TAG = "main";

static void app_task(void *pvParameters)
{

    while(1)
    {
    	int16_t count;

        uint16_t sample = maxbotix_get_latest();

        ESP_LOGI(TAG, "received sample %d", sample);

        int32_t age = maxbotix_get_age();
        float result = maxbotix_get_median(0.6f,8,32,&count);
        ESP_LOGI(TAG,"Median sample returned %f, sample count %d",(double)result,count);
        

        /* Wait delay for 2 second interval */
        vTaskDelay(pdMS_TO_TICKS(1000*2));
    }
}

void app_main(void)
{
    ESP_LOGW(TAG, "Starting maxbotics sensor test");

    /* Call Maxbotix function */
    maxbotix_init();

    ESP_LOGW(TAG, "inited the driver");

    /* Reduce logging from maxbotix */
    // esp_log_level_set("maxbotix", ESP_LOG_WARN);

    /* Start main task at lower priority */
    // xTaskCreatePinnedToCore(app_task, "app_task", 2048, NULL, 1, NULL,1);

	xTaskCreate(
	    app_task,       // Task function
	    "app_task",     // Name of the task (useful for debugging)
	    2048,           // Stack size in words (not bytes)
	    NULL,           // Parameter passed to the task
	    20,              // Task priority (1 is a low priority)
	    NULL            // Task handle (can be NULL if not needed)
	);

    ESP_LOGW(TAG, "created the task");

	// Keep main alive
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));  // Delay for 1000ms (1 second)
    }

}