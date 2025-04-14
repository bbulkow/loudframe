#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include "freertos/FreeRTOS.h"
#include "esp_log.h"

esp_err_t init_i2s_std(void);

void play_sine_wave(float frequency, float amplitude);