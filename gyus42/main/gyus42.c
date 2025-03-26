#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/i2c_master.h"

// Author: Brian Bulkowski
// Could not find github esp-idf source code for interfacing with the 
// GY-US42v2 sensor over I2C
//
// using https://www.yoctopuce.com/EN/article/interfacing-the-gy-us42v2-sonar
// Which states:
// The default I2C address is 0x70. This mean that I2C write operations will always start with 2*0x70 = 0xE0 and read operation will start with 2*0x70+1 = 0xE1. 
// To start a measure , with a result in centimeters, one has to send the 0x51 command. 
// The measure total duration might vary, but the maximum is 65ms. 
// To read the result, one needs to read two bytes from the sensor which are actually a big endian encoded word. 

static const char *TAG = "main";

#define GYUS42_SCL_IO               22                     /*!< GPIO number used for I2C master clock */
#define GYUS42_SDA_IO               21                        /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              I2C_NUM_1                   /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ                 100000 /* While this sensor and esp32 probably support 400khz, it's unnecessary because it's only a few bytes */
#define I2C_MASTER_TX_BUF_DISABLE   0                           /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE   0                           /*!< I2C master doesn't need buffer */
#define I2C_MASTER_TIMEOUT_MS       1000

#define GYUS42_SENSOR_ADDR          0x70             /*!< According to YoctoPuce, can't be changed, although others can */
#define GYUS42_CMD_GET_DISTANCE     0x51             /*!< Register addresses of the "get distance" command */

/**
 * @brief Read a sequence of bytes from a GYUS42 sensor
 * Note, most libraries of this type allow you to specify the sensor address as you might have several
 * But this doesn't allow you to change the sensor address apparently so let's have a simpler interface
 * 
 * Further note. It would be better to bundle this into a measure function. Yeah, yeah.
 */
static esp_err_t gyus42_read(i2c_master_dev_handle_t dev_handle, uint8_t *data, size_t len)
{
    // note: it's possible this could be changed to transmit_receive
    // uint8_t reg_addr = GYUS42_SENSOR_ADDR;
    // note this function never receives an incomplete number of bytes. It timesout instead
    return i2c_master_receive(dev_handle, data, len, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}

/**
 * @brief Write a byte to a GYUS42 sensor
 */
static esp_err_t gyus42_write_byte(i2c_master_dev_handle_t dev_handle, uint8_t data)
{
    return i2c_master_transmit(dev_handle, &data, sizeof(data), I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
}


/**
 * @brief i2c master initialization
 */
static void i2c_master_init(i2c_master_bus_handle_t *bus_handle, i2c_master_dev_handle_t *dev_handle)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_MASTER_NUM,
        .sda_io_num = GYUS42_SDA_IO,
        .scl_io_num = GYUS42_SCL_IO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, bus_handle));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = GYUS42_SENSOR_ADDR,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(*bus_handle, &dev_config, dev_handle));
}

void app_main(void)
{
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
    i2c_master_init(&bus_handle, &dev_handle);
    ESP_LOGI(TAG, "I2C initialized successfully");

    // Let's do a probe and see if the device is properly wired n what not
    esp_err_t ret = i2c_master_probe(bus_handle, GYUS42_SENSOR_ADDR, -1);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "GYUS42 sensor found via probe");
    }
    else {
        ESP_LOGW(TAG, "GYUS42 sensor not found, error %d %s",ret,esp_err_to_name(ret));
        esp_restart();
    }


    // Get distances
    do {
        // note there is also a function write and read....

        /* Write the byte asking for a measurement */
        ESP_ERROR_CHECK( gyus42_write_byte(dev_handle, GYUS42_CMD_GET_DISTANCE) );

        // not sure if I should add a delay here. Often takes 40ms or so to get an echo back.
        // timeout should be great enough to allow that????

        uint8_t data[2];
        ESP_ERROR_CHECK( gyus42_read(dev_handle, &data[0], sizeof(data)));

        // big endian encoded centimeters
        int distance = (data[0] << 8) | data[1];

        ESP_LOGI(TAG, "read distance: %d cm",distance);

        vTaskDelay(pdMS_TO_TICKS(1000));

    } while (1);

    ESP_ERROR_CHECK(i2c_master_bus_rm_device(dev_handle));
    ESP_ERROR_CHECK(i2c_del_master_bus(bus_handle));
    ESP_LOGI(TAG, "I2C de-initialized successfully");
}
