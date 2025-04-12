/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2019 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
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

#ifndef _ESXXX_COMMON_H_
#define _ESXXX_COMMON_H_

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ES_RATE_22KHZ (22050)
#define ES_RATE_44KHZ (44100)
#define ES_RATE_48KHZ (48000)

typedef enum {
    BIT_LENGTH_MIN = -1,
    BIT_LENGTH_16BITS = 0x03,
    BIT_LENGTH_18BITS = 0x02,
    BIT_LENGTH_20BITS = 0x01,
    BIT_LENGTH_24BITS = 0x00,
    BIT_LENGTH_32BITS = 0x04,
    BIT_LENGTH_MAX,
} es_bits_length_t;

typedef enum {
    MCLK_DIV_MIN = -1,
    MCLK_DIV_1 = 1,
    MCLK_DIV_2 = 2,
    MCLK_DIV_3 = 3,
    MCLK_DIV_4 = 4,
    MCLK_DIV_6 = 5,
    MCLK_DIV_8 = 6,
    MCLK_DIV_9 = 7,
    MCLK_DIV_11 = 8,
    MCLK_DIV_12 = 9,
    MCLK_DIV_16 = 10,
    MCLK_DIV_18 = 11,
    MCLK_DIV_22 = 12,
    MCLK_DIV_24 = 13,
    MCLK_DIV_33 = 14,
    MCLK_DIV_36 = 15,
    MCLK_DIV_44 = 16,
    MCLK_DIV_48 = 17,
    MCLK_DIV_66 = 18,
    MCLK_DIV_72 = 19,
    MCLK_DIV_5 = 20,
    MCLK_DIV_10 = 21,
    MCLK_DIV_15 = 22,
    MCLK_DIV_17 = 23,
    MCLK_DIV_20 = 24,
    MCLK_DIV_25 = 25,
    MCLK_DIV_30 = 26,
    MCLK_DIV_32 = 27,
    MCLK_DIV_34 = 28,
    MCLK_DIV_7  = 29,
    MCLK_DIV_13 = 30,
    MCLK_DIV_14 = 31,
    MCLK_DIV_MAX,
} es_sclk_div_t;

typedef enum {
    LCLK_DIV_MIN = -1,
    LCLK_DIV_128 = 0,
    LCLK_DIV_192 = 1,
    LCLK_DIV_256 = 2,
    LCLK_DIV_384 = 3,
    LCLK_DIV_512 = 4,
    LCLK_DIV_576 = 5,
    LCLK_DIV_768 = 6,
    LCLK_DIV_1024 = 7,
    LCLK_DIV_1152 = 8,
    LCLK_DIV_1408 = 9,
    LCLK_DIV_1536 = 10,
    LCLK_DIV_2112 = 11,
    LCLK_DIV_2304 = 12,

    LCLK_DIV_125 = 16,
    LCLK_DIV_136 = 17,
    LCLK_DIV_250 = 18,
    LCLK_DIV_272 = 19,
    LCLK_DIV_375 = 20,
    LCLK_DIV_500 = 21,
    LCLK_DIV_544 = 22,
    LCLK_DIV_750 = 23,
    LCLK_DIV_1000 = 24,
    LCLK_DIV_1088 = 25,
    LCLK_DIV_1496 = 26,
    LCLK_DIV_1500 = 27,
    LCLK_DIV_MAX,
} es_lclk_div_t;

typedef enum {
    D2SE_PGA_GAIN_MIN = -1,
    D2SE_PGA_GAIN_DIS = 0,
    D2SE_PGA_GAIN_EN = 1,
    D2SE_PGA_GAIN_MAX = 2,
} es_d2se_pga_t;


typedef enum {
    ADC_INPUT_MIN = -1,
    ADC_INPUT_DISABLE,
    ADC_INPUT_LINPUT1_RINPUT1 = 0x00,
    ADC_INPUT_MIC1  = 0x05,
    ADC_INPUT_MIC2  = 0x06,
    ADC_INPUT_LINPUT2_RINPUT2 = 0x50,
    ADC_INPUT_DIFFERENCE = 0xf0,
    ADC_INPUT_MAX,
} es_adc_input_t;

// this doesn't seem to really match the data sheet. It's the settings for register 04.
// PdnDACL - 7 - left dac power - 0 up, 1 down
// PdnDACR - 6 - right DAC power - 0 up, 1 down
// LOUT1 - 5 - LOUT1 - 0 disabled (default) 1 enabled
// ROUT1 - 5 - ROUT1 - 0 disabled (default) 1 enabled
// LOUT2 - 5 - LOUT2 - 0 disabled (default) 1 enabled
// ROUT2 - 5 - ROUT2 - 0 disabled (default) 1 enabled
typedef enum {
    DAC_OUTPUT_MIN = -1,
    DAC_OUTPUT_OFF = 0xC0,
    DAC_OUTPUT_LOUT_PWR = 0x80,
    DAC_OUTPUT_ROUT_PWR = 0x40,
    DAC_OUTPUT_LOUT1 = 0x20,
    DAC_OUTPUT_LOUT2 = 0x08,
    DAC_OUTPUT_ROUT1 = 0x10,
    DAC_OUTPUT_ROUT2 = 0x04,
    DAC_OUTPUT_ALL = 0xFC,
    DAC_OUTPUT_MAX,
} es_dac_output_t;

typedef enum {
    MIC_GAIN_MIN = -1,
    MIC_GAIN_0DB = 0,
    MIC_GAIN_3DB = 3,
    MIC_GAIN_6DB = 6,
    MIC_GAIN_9DB = 9,
    MIC_GAIN_12DB = 12,
    MIC_GAIN_15DB = 15,
    MIC_GAIN_18DB = 18,
    MIC_GAIN_21DB = 21,
    MIC_GAIN_24DB = 24,
    MIC_GAIN_MAX,
} es_mic_gain_t;

typedef enum {
    ES_MODULE_MIN = -1,
    ES_MODULE_ADC = 0x01,
    ES_MODULE_DAC = 0x02,
    ES_MODULE_ADC_DAC = 0x03,
    ES_MODULE_LINE = 0x04,
    ES_MODULE_MAX
} es_module_t;

// typedef enum {
//     ES_MODE_MIN = -1,
//     ES_MODE_SLAVE = 0x00,
//     ES_MODE_MASTER = 0x01,
//     ES_MODE_MAX,
// } es_mode_t;

typedef enum {
    ES_I2S_MIN = -1,
    ES_I2S_NORMAL = 0,
    ES_I2S_LEFT = 1,
    ES_I2S_RIGHT = 2,
    ES_I2S_DSP = 3,
    ES_I2S_MAX
} es_i2s_fmt_t;

/**
 * @brief Configure ES8388 clock
 */
typedef struct {
    es_sclk_div_t sclk_div;    /*!< bits clock divide */
    es_lclk_div_t lclk_div;    /*!< WS clock divide */
} es_i2s_clock_t;

// from audio_hal.h
// most of these will have to be removed over time and thus
// should be replaced with es_ . However we find there are already
// these definitions, so let's use those.

// /**
//  * @brief Select adc channel for input mic signal
//  */
// typedef enum {
//     ES_ADC_INPUT_LINE1 = 0x00,  /*!< mic input to adc channel 1 */
//     ES_ADC_INPUT_LINE2,         /*!< mic input to adc channel 2 */
//     ES_ADC_INPUT_ALL,           /*!< mic input to both channels of adc */
//     ES_ADC_INPUT_DIFFERENCE,    /*!< mic input to adc difference channel */
// } es_adc_input_t;

// /**
//  * @brief Select channel for dac output
//  */
// typedef enum {
//     ES_DAC_OUTPUT_LINE1 = 0x00,  /*!< dac output signal to channel 1 */
//     ES_DAC_OUTPUT_LINE2,         /*!< dac output signal to channel 2 */
//     ES_DAC_OUTPUT_ALL,           /*!< dac output signal to both channels */
// } es_dac_output_t;

/**
 * @brief Select media hal codec mode
 */
typedef enum {
    ES_CODEC_MODE_ENCODE = 1,  /*!< select adc */
    ES_CODEC_MODE_DECODE,      /*!< select dac */
    ES_CODEC_MODE_BOTH,        /*!< select both adc and dac */
    ES_CODEC_MODE_LINE_IN,     /*!< set adc channel */
} es_codec_mode_t;

typedef enum {
    ES_MODE_SLAVE = 0,  /*!< Slave mode */
    ES_MODE_MASTER      /*!< Master mode */
} es_iface_mode_t;

// es_i2s_fmt >
// typedef enum {
//     ES_I2S_NORMAL = 0,  /*!< Standard I2S format */
//     ES_I2S_LEFT,        /*!< Left-justified format */
//     ES_I2S_RIGHT,       /*!< Right-justified format */
//     ES_I2S_DSP_A,       /*!< DSP/PCM A mode */
//     ES_I2S_DSP_B        /*!< DSP/PCM B mode */
// } es_iface_format_t;

typedef enum {
    ES_BIT_LENGTH_8BITS = 8,
    ES_BIT_LENGTH_16BITS = 16,
    ES_BIT_LENGTH_24BITS = 24,
    ES_BIT_LENGTH_32BITS = 32
} es_bit_length_t;


typedef struct {
    es_iface_mode_t mode;      /*!< Audio interface operating mode: master or slave */
//    es_iface_format_t fmt;     /*!< Audio interface format */
    es_i2s_fmt_t fmt;           /*!< Audio interface format */
    int samples;                      /*!< Number of samples per second (sampling rate) */
    es_bit_length_t bits;     /*!< Audio bit depth */
} es_codec_i2s_iface_t;


// typedef enum {
//     ES_CTRL_CODEC = 1,     /*!< Control all codec functions */
//     ES_CTRL_ADC = 2,       /*!< Control ADC only */
//     ES_CTRL_DAC = 3        /*!< Control DAC only */
// } es_ctrl_t;

typedef enum {
    ES_CTRL_START,    
    ES_CTRL_STOP     
} es_ctrl_t;




/**
 * @brief Configure media hal for initialization of audio codec chip
 */
typedef struct {
    es_adc_input_t adc_input;       /*!< set adc channel */
    es_dac_output_t dac_output;     /*!< set dac channel */
    es_codec_mode_t codec_mode;     /*!< select codec mode: adc, dac or both */
    es_codec_i2s_iface_t i2s_iface; /*!< set I2S interface configuration */
} es_codec_config_t;


#if 0

#define AUDIO_HAL_VOL_DEFAULT 70

typedef struct audio_hal *audio_hal_handle_t;

/**
 * @brief Select media hal codec mode
 */
typedef enum {
    AUDIO_HAL_CODEC_MODE_ENCODE = 1,  /*!< select adc */
    AUDIO_HAL_CODEC_MODE_DECODE,      /*!< select dac */
    AUDIO_HAL_CODEC_MODE_BOTH,        /*!< select both adc and dac */
    AUDIO_HAL_CODEC_MODE_LINE_IN,     /*!< set adc channel */
} audio_hal_codec_mode_t;

/**
 * @brief Select adc channel for input mic signal
 */
typedef enum {
    AUDIO_HAL_ADC_INPUT_LINE1 = 0x00,  /*!< mic input to adc channel 1 */
    AUDIO_HAL_ADC_INPUT_LINE2,         /*!< mic input to adc channel 2 */
    AUDIO_HAL_ADC_INPUT_ALL,           /*!< mic input to both channels of adc */
    AUDIO_HAL_ADC_INPUT_DIFFERENCE,    /*!< mic input to adc difference channel */
} audio_hal_adc_input_t;

/**
 * @brief Select channel for dac output
 */
typedef enum {
    AUDIO_HAL_DAC_OUTPUT_LINE1 = 0x00,  /*!< dac output signal to channel 1 */
    AUDIO_HAL_DAC_OUTPUT_LINE2,         /*!< dac output signal to channel 2 */
    AUDIO_HAL_DAC_OUTPUT_ALL,           /*!< dac output signal to both channels */
} audio_hal_dac_output_t;

/**
 * @brief Select operating mode i.e. start or stop for audio codec chip
 */
typedef enum {
    AUDIO_HAL_CTRL_STOP  = 0x00,  /*!< set stop mode */
    AUDIO_HAL_CTRL_START = 0x01,  /*!< set start mode */
} audio_hal_ctrl_t;

/**
 * @brief Select I2S interface operating mode i.e. master or slave for audio codec chip
 */
typedef enum {
    AUDIO_HAL_MODE_SLAVE = 0x00,   /*!< set slave mode */
    AUDIO_HAL_MODE_MASTER = 0x01,  /*!< set master mode */
} audio_hal_iface_mode_t;

/**
 * @brief Select I2S interface samples per second
 */
typedef enum {
    AUDIO_HAL_08K_SAMPLES,   /*!< set to  8k samples per second */
    AUDIO_HAL_11K_SAMPLES,   /*!< set to 11.025k samples per second */
    AUDIO_HAL_16K_SAMPLES,   /*!< set to 16k samples in per second */
    AUDIO_HAL_22K_SAMPLES,   /*!< set to 22.050k samples per second */
    AUDIO_HAL_24K_SAMPLES,   /*!< set to 24k samples in per second */
    AUDIO_HAL_32K_SAMPLES,   /*!< set to 32k samples in per second */
    AUDIO_HAL_44K_SAMPLES,   /*!< set to 44.1k samples per second */
    AUDIO_HAL_48K_SAMPLES,   /*!< set to 48k samples per second */
} audio_hal_iface_samples_t;

/**
 * @brief Select I2S interface number of bits per sample
 */
typedef enum {
    AUDIO_HAL_BIT_LENGTH_16BITS = 1,   /*!< set 16 bits per sample */
    AUDIO_HAL_BIT_LENGTH_24BITS,       /*!< set 24 bits per sample */
    AUDIO_HAL_BIT_LENGTH_32BITS,       /*!< set 32 bits per sample */
} audio_hal_iface_bits_t;

/**
 * @brief Select I2S interface format for audio codec chip
 */
typedef enum {
    AUDIO_HAL_I2S_NORMAL = 0,  /*!< set normal I2S format */
    AUDIO_HAL_I2S_LEFT,        /*!< set all left format */
    AUDIO_HAL_I2S_RIGHT,       /*!< set all right format */
    AUDIO_HAL_I2S_DSP,         /*!< set dsp/pcm format */
} audio_hal_iface_format_t;

/**
 * @brief I2s interface configuration for audio codec chip
 */
typedef struct {
    audio_hal_iface_mode_t mode;        /*!< audio codec chip mode */
    audio_hal_iface_format_t fmt;       /*!< I2S interface format */
    audio_hal_iface_samples_t samples;  /*!< I2S interface samples per second */
    audio_hal_iface_bits_t bits;        /*!< i2s interface number of bits per sample */
} audio_hal_codec_i2s_iface_t;

/**
 * @brief Configure media hal for initialization of audio codec chip
 */
typedef struct {
    audio_hal_adc_input_t adc_input;       /*!< set adc channel */
    audio_hal_dac_output_t dac_output;     /*!< set dac channel */
    audio_hal_codec_mode_t codec_mode;     /*!< select codec mode: adc, dac or both */
    audio_hal_codec_i2s_iface_t i2s_iface; /*!< set I2S interface configuration */
} audio_hal_codec_config_t;

/**
 * @brief Configuration of functions and variables used to operate audio codec chip
 */
typedef struct audio_hal {
    esp_err_t (*audio_codec_initialize)(audio_hal_codec_config_t *codec_cfg);                                /*!< initialize codec */
    esp_err_t (*audio_codec_deinitialize)(void);                                                             /*!< deinitialize codec */
    esp_err_t (*audio_codec_ctrl)(audio_hal_codec_mode_t mode, audio_hal_ctrl_t ctrl_state);                 /*!< control codec mode and state */
    esp_err_t (*audio_codec_config_iface)(audio_hal_codec_mode_t mode, audio_hal_codec_i2s_iface_t *iface);  /*!< configure i2s interface */
    esp_err_t (*audio_codec_set_mute) (bool mute);                                                           /*!< set codec mute */
    esp_err_t (*audio_codec_set_volume)(int volume);                                                         /*!< set codec volume */
    esp_err_t (*audio_codec_get_volume)(int *volume);                                                        /*!< get codec volume */
    esp_err_t (*audio_codec_enable_pa) (bool enable);                                                        /*!< enable pa */
    SemaphoreHandle_t audio_hal_lock;                                                                         /*!< semaphore of codec */
    void *handle;                                                                                            /*!< handle of audio codec */
} audio_hal_func_t;

#endif


#ifdef __cplusplus
}
#endif

#endif
