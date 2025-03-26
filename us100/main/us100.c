#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "driver/uart.h"

/* Component includes */
#include "esp32-uart-us-100.h"

// Author: Brian Bulkowski brian@bulkowski.org (C) 2025
// Taken as example from us-test.c and JSpeedie

/* Going off  https://learn.adafruit.com/assets/111179 */

#define UART_PORT_NUM UART_NUM_1
/* Match these to your UC 100. The RX is wired to RX and TX to TX. */
#define UART_RX_PIN_NUM 21
#define UART_TX_PIN_NUM 22


void app_main(void)
{
	/* UART Device Configuration {{{ */
	/* 1. Set UART Commynication Parameters */
    const uart_port_t uart_num = UART_PORT_NUM;
    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    ESP_ERROR_CHECK(uart_param_config(uart_num, &uart_config));

    /* 2. Set UART pins (On UART port 2, set TX, RX, and do not make use of RTS or CTS) */
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT_NUM, UART_TX_PIN_NUM, UART_RX_PIN_NUM, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    /* 3. Allocate resources for the UART driver (buffered IO with event queue)
     * and install the driver */
    /* Multiply by 2 because some of this buffer will be for RX, and some of it for TX */
    const int uart_buffer_size = (1024 * 2);
    QueueHandle_t uart_queue;

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT_NUM, uart_buffer_size, \
        uart_buffer_size, 10, &uart_queue, 0));


	/* 3a. Perform device specific initialization */
	struct esp_uart_us_100 uart_us_sensor = esp_uart_us_100_init(UART_PORT_NUM);

    uint16_t distance = 0;
    int8_t temperature = 0;

	while (1) {
        /* Each of the below US-100 function calls takes at maximum 500ms */
        if (0 != esp_uart_us_100_read_distance(&uart_us_sensor, &distance)) {
            printf("ERROR: could not read 2 bytes representing the distance measurement from US-100\n");
        } else {
            printf("Ultrasonic sensor senses an object %d mm away!\n", distance);
        }

        if (0 != esp_uart_us_100_read_temperature(&uart_us_sensor, &temperature)) {
            printf("ERROR: could not read 1 byte representing the temperature measurement from US-100\n");
        } else {
            printf("Ultrasonic sensor has a temperature of %d ° C!\n", temperature);
        }

        /* You can set this delay to 0, it just makes the app output to stdout
         * a bit too fast to be comfortably read by the human eye */
		vTaskDelay(pdMS_TO_TICKS(500));
	}
}

