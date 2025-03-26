#ifndef __ESP32_UART_US_100_
#define __ESP32_UART_US_100_

#include <stdio.h>
#include <inttypes.h>

#include "driver/uart.h"


/* Ultrasonic Sensor Constants */
/* Constants for command types */
#define US_READDISTANCE 0x55
#define US_READTEMPERATURE 0x50\

#define US_READDISTANCE_LEN 2


struct esp_uart_us_100 {
	uart_port_t uart_port_num;
};


union temperature_conversion {
	uint8_t raw_data;
	int8_t temp;
};


struct esp_uart_us_100 esp_uart_us_100_init(uart_port_t uart_num);

int esp_uart_us_100_read_distance(struct esp_uart_us_100 *esp_us_sensor, uint16_t *ret);

int esp_uart_us_100_read_temperature(struct esp_uart_us_100 *esp_us_sensor, int8_t *ret);

#endif
