#include <stdio.h>
#include <inttypes.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "driver/uart.h"

#include "esp32-uart-us-100.h"

static const char* TAG = "US100-driver";


struct esp_uart_us_100 esp_uart_us_100_init(uart_port_t uart_num) {
	/* {{{ */

	struct esp_uart_us_100 ret = {
		.uart_port_num = uart_num,
	};

    ESP_ERROR_CHECK(uart_flush(uart_num));

	return ret;
	/* }}} */
}

// note: the data sheet states that the first byte is MSB and the second byte is LSB.
// But, the ESP32 is little endian. Therefore one needs to swap.

int esp_uart_us_100_read_distance(struct esp_uart_us_100 *esp_us_sensor, uint16_t *ret) {
	/* {{{ */
    ESP_ERROR_CHECK(uart_flush(esp_us_sensor->uart_port_num));

    /* Tell the sensor what data we would like it to return */
    char command = US_READDISTANCE;
    uart_write_bytes(esp_us_sensor->uart_port_num, (const char *) &command, 1);

    /* Delay to give the US-100 time to respond */
    size_t length = 0;
    do {
        vTaskDelay(pdMS_TO_TICKS(10));
        ESP_ERROR_CHECK( uart_get_buffered_data_len(esp_us_sensor->uart_port_num, &length) );
    } while (length < US_READDISTANCE_LEN);

    ESP_LOGD(TAG, "read from US100: %d bytes to read", (int)length);

    /* Regardless of the length of the buffered data, read 2 bytes */
    uint8_t data[US_READDISTANCE_LEN];
    if (US_READDISTANCE_LEN != \
        uart_read_bytes(esp_us_sensor->uart_port_num, &data, US_READDISTANCE_LEN, pdMS_TO_TICKS(400))) {
        ESP_LOGW(TAG, "failure reading");
        return -1;
    }

    ESP_LOGD(TAG, "read from US100: %d bytes to read, %x byte1 %x byte2", length, data[0], data[1]);

    *ret = (data[0] << 8) | data[1];

    return 0;
	/* }}} */
}


int esp_uart_us_100_read_temperature(struct esp_uart_us_100 *esp_us_sensor, int8_t *ret) {
	/* {{{ */
    ESP_ERROR_CHECK(uart_flush(esp_us_sensor->uart_port_num));

    /* Tell the sensor what data we would like it to return */
    char command = US_READTEMPERATURE;
    uart_write_bytes(esp_us_sensor->uart_port_num, (const char *) &command, 1);

    /* Delay to give the US-100 time to respond */
	vTaskDelay(pdMS_TO_TICKS(100));

    /* Read the response data from the ultrasonic sensor */
    union temperature_conversion data; // Temeperature responses from the US-100 will always be one byte long Temeperature responses from the US-100 will always be one byte long
    size_t length = 0;
    /* Check the length of the data in the RX buffer */
    ESP_ERROR_CHECK( \
        uart_get_buffered_data_len(esp_us_sensor->uart_port_num, &length));

    /* Regardless of the length of the buffered data, read 1 byte */
    if (sizeof(data) != \
        uart_read_bytes(esp_us_sensor->uart_port_num, &(data.raw_data), sizeof(data.raw_data), pdMS_TO_TICKS(400))) {

        return -1;
    }

    /* Temperature data is stored with +45 degrees Celsius which we must
     * subtract to get the correct value */
    *ret = data.temp - 45;
    return 0;

	/* }}} */
}
