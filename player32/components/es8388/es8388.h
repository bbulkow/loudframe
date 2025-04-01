/*
 * MIT License
 *
 * Copyright (c) 2025 Brian Bulkowski brian@bulkowski.org
 * 
 * While this header originally started from the Espressif ES8388 header,
 * conversion to new I2C interface, and prior confusing names, makes it a substantially
 * new work.
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

#ifndef __ES8388_H__
#define __ES8388_H__

#include "esp_types.h"
#include "driver/i2c.h"
#include "esxxx_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ES8388 address */
// BB: note, the old interface uses << 1, the new uses the normal value
// #define ES8388_ADDR 0x20  /*!< 0x22:CE=1;0x20:CE=0*/
#define ES8388_ADDR 0x10  /*!< 0x22:CE=1;0x20:CE=0*/

// /* ES8388 register */
// these appear to be the OLD VERSIONS based on the old method of I2C
// #define ES8388_CONTROL1         0x00
// #define ES8388_CONTROL2         0x01

// #define ES8388_CHIPPOWER        0x02

// #define ES8388_ADCPOWER         0x03
// #define ES8388_DACPOWER         0x04

// #define ES8388_CHIPLOPOW1       0x05
// #define ES8388_CHIPLOPOW2       0x06

// #define ES8388_ANAVOLMANAG      0x07

// #define ES8388_MASTERMODE       0x08
// /* ADC */
// #define ES8388_ADCCONTROL1      0x09
// #define ES8388_ADCCONTROL2      0x0a
// #define ES8388_ADCCONTROL3      0x0b
// #define ES8388_ADCCONTROL4      0x0c
// #define ES8388_ADCCONTROL5      0x0d
// #define ES8388_ADCCONTROL6      0x0e
// #define ES8388_ADCCONTROL7      0x0f
// #define ES8388_ADCCONTROL8      0x10
// #define ES8388_ADCCONTROL9      0x11
// #define ES8388_ADCCONTROL10     0x12
// #define ES8388_ADCCONTROL11     0x13
// #define ES8388_ADCCONTROL12     0x14
// #define ES8388_ADCCONTROL13     0x15
// #define ES8388_ADCCONTROL14     0x16
// /* DAC */
// #define ES8388_DACCONTROL1      0x17
// #define ES8388_DACCONTROL2      0x18
// #define ES8388_DACCONTROL3      0x19
// #define ES8388_DACCONTROL4      0x1a
// #define ES8388_DACCONTROL5      0x1b
// #define ES8388_DACCONTROL6      0x1c
// #define ES8388_DACCONTROL7      0x1d
// #define ES8388_DACCONTROL8      0x1e
// #define ES8388_DACCONTROL9      0x1f
// #define ES8388_DACCONTROL10     0x20
// #define ES8388_DACCONTROL11     0x21
// #define ES8388_DACCONTROL12     0x22
// #define ES8388_DACCONTROL13     0x23
// #define ES8388_DACCONTROL14     0x24
// #define ES8388_DACCONTROL15     0x25
// #define ES8388_DACCONTROL16     0x26
// #define ES8388_DACCONTROL17     0x27
// #define ES8388_DACCONTROL18     0x28
// #define ES8388_DACCONTROL19     0x29
// #define ES8388_DACCONTROL20     0x2a
// #define ES8388_DACCONTROL21     0x2b
// #define ES8388_DACCONTROL22     0x2c
// #define ES8388_DACCONTROL23     0x2d
// #define ES8388_DACCONTROL24     0x2e
// #define ES8388_DACCONTROL25     0x2f
// #define ES8388_DACCONTROL26     0x30
// #define ES8388_DACCONTROL27     0x31
// #define ES8388_DACCONTROL28     0x32
// #define ES8388_DACCONTROL29     0x33
// #define ES8388_DACCONTROL30     0x34

// Please see:
// https://media.digikey.com/pdf/Data%20Sheets/ITW%20Chemtronics/ES8388.pdf


// Chip Control and Power Management Registers
#define ES8388_CONTROL1         0x00  // Chip Control 1
#define ES8388_CONTROL2         0x01  // Chip Control 2
#define ES8388_CHIPPOWER        0x02  // Chip Power Management
#define ES8388_ADCPOWER         0x03  // ADC Power Management
#define ES8388_DACPOWER         0x04  // DAC Power Management
#define ES8388_CHIPLOPOW1       0x05  // Chip Low Power 1
#define ES8388_CHIPLOPOW2       0x06  // Chip Low Power 2
#define ES8388_ANAVOLMANAG      0x07  // Analog Voltage Management
#define ES8388_MASTERMODE       0x08  // Master Mode Control

// ADC Control Registers
#define ES8388_ADCCONTROL1      0x09  // ADC Control 1
#define ES8388_ADCCONTROL2      0x0A  // ADC Control 2
#define ES8388_ADCCONTROL3      0x0B  // ADC Control 3
#define ES8388_ADCCONTROL4      0x0C  // ADC Control 4
#define ES8388_ADCCONTROL5      0x0D  // ADC Control 5
#define ES8388_ADCCONTROL6      0x0E  // ADC Control 6
#define ES8388_ADCCONTROL7      0x0F  // ADC Control 7
#define ES8388_ADCCONTROL8      0x10  // ADC Control 8
#define ES8388_ADCCONTROL9      0x11  // ADC Control 9
#define ES8388_ADCCONTROL10     0x12  // ADC Control 10
#define ES8388_ADCCONTROL11     0x13  // ADC Control 11
#define ES8388_ADCCONTROL12     0x14  // ADC Control 12
#define ES8388_ADCCONTROL13     0x15  // ADC Control 13
#define ES8388_ADCCONTROL14     0x16  // ADC Control 14

// DAC Control Registers
#define ES8388_DACCONTROL1      0x17  // DAC Control 1
#define ES8388_DACCONTROL2      0x18  // DAC Control 2
#define ES8388_DACCONTROL3      0x19  // DAC Control 3
#define ES8388_LDACVOL          0x1A  // Left DAC Volume Control
#define ES8388_RDACVOL          0x1B  // Right DAC Volume Control
#define ES8388_DACCONTROL6      0x1C  // DAC Control 6
#define ES8388_DACCONTROL7      0x1D  // DAC Control 7
#define ES8388_DACCONTROL8      0x1E  // DAC Control 8
#define ES8388_DACCONTROL9      0x1F  // DAC Control 9
#define ES8388_DACCONTROL10     0x20  // DAC Control 10
#define ES8388_DACCONTROL11     0x21  // DAC Control 11
#define ES8388_DACCONTROL12     0x22  // DAC Control 12
#define ES8388_DACCONTROL13     0x23  // DAC Control 13
#define ES8388_DACCONTROL14     0x24  // DAC Control 14
#define ES8388_DACCONTROL15     0x25  // DAC Control 15
#define ES8388_DACCONTROL16     0x26  // DAC Control 16
#define ES8388_DACCONTROL17     0x27  // DAC Control 17
#define ES8388_DACCONTROL18     0x28  // DAC Control 18
#define ES8388_DACCONTROL19     0x29  // DAC Control 19
#define ES8388_DACCONTROL20     0x2A  // DAC Control 20
#define ES8388_DACCONTROL21     0x2B  // DAC Control 21
#define ES8388_DACCONTROL22     0x2C  // DAC Control 22
#define ES8388_DACCONTROL23     0x2D  // DAC Control 23
#define ES8388_DACCONTROL24     0x2E  // DAC Control 24
#define ES8388_DACCONTROL25     0x2F  // DAC Control 25
#define ES8388_DACCONTROL26     0x30  // DAC Control 26
#define ES8388_DACCONTROL27     0x31  // DAC Control 27
#define ES8388_DACCONTROL28     0x32  // DAC Control 28
#define ES8388_DACCONTROL29     0x33  // DAC Control 29
#define ES8388_DACCONTROL30     0x34  // DAC Control 30
#define ES8388_DACCONTROL31     0x35  // DAC Control 31
#define ES8388_DACCONTROL32     0x36  // DAC Control 32
#define ES8388_DACCONTROL33     0x37  // DAC Control 33

/**
 * @brief Initialize ES8388 codec chip
 *
 * @param cfg configuration of ES8388
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t es8388_init(audio_hal_codec_config_t *cfg);

/**
 * @brief Deinitialize ES8388 codec chip
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t es8388_deinit(void);

/**
 * @brief Configure ES8388 I2S format
 *
 * @param mod:  set ADC or DAC or both
 * @param cfg:   ES8388 I2S format
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t es8388_config_fmt(es_module_t mod, es_i2s_fmt_t cfg);

/**
 * @brief Configure I2s clock in MSATER mode
 *
 * @param cfg:  set bits clock and WS clock
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t es8388_i2s_config_clock(es_i2s_clock_t cfg);

/**
 * @brief Configure ES8388 data sample bits
 *
 * @param mode:  set ADC or DAC or both
 * @param bit_per_sample:  bit number of per sample
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t es8388_set_bits_per_sample(es_module_t mode, es_bits_length_t bit_per_sample);

/**
 * @brief  Start ES8388 codec chip
 *
 * @param mode:  set ADC or DAC or both
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t es8388_start(es_module_t mode);

/**
 * @brief  Stop ES8388 codec chip
 *
 * @param mode:  set ADC or DAC or both
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t es8388_stop(es_module_t mode);

/**
 * @brief  Set voice volume
 *
 * @param volume:  voice volume (0~100)
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t es8388_set_volume(int volume);

/**
 * @brief Get voice volume
 *
 * @param[out] *volume:  voice volume (0~100)
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */
esp_err_t es8388_get_volume(int *volume);

/**
 * @brief Configure ES8388 DAC mute or not. Basically you can use this function to mute the output or unmute
 *
 * @param enable enable(1) or disable(0)
 *
 * @return
 *     - ESP_FAIL Parameter error
 *     - ESP_OK   Success
 */
esp_err_t es8388_set_mute(bool enable);

/**
 * @brief Get ES8388 DAC mute status
 *
 *  @return
 *     - ESP_FAIL Parameter error
 *     - ESP_OK   Success
 */
esp_err_t es8388_get_mute(void);

/**
 * @brief Set ES8388 mic gain
 *
 * @param gain db of mic gain
 *
 * @return
 *     - ESP_FAIL Parameter error
 *     - ESP_OK   Success
 */
esp_err_t es8388_set_mic_gain(es_mic_gain_t gain);

/**
 * @brief Set ES8388 adc input mode
 *
 * @param input adc input mode
 *
 * @return
 *     - ESP_FAIL Parameter error
 *     - ESP_OK   Success
 */
esp_err_t es8388_config_adc_input(es_adc_input_t input);

/**
 * @brief Set ES8388 dac output mode
 *
 * @param output dac output mode
 *
 * @return
 *     - ESP_FAIL Parameter error
 *     - ESP_OK   Success
 */
esp_err_t es8388_config_dac_output(es_dac_output_t output);

/**
 * @brief Write ES8388 register
 *
 * @param reg_add address of register
 * @param data data of register
 *
 * @return
 *     - ESP_FAIL Parameter error
 *     - ESP_OK   Success
 */
esp_err_t es8388_write_reg(uint8_t reg_add, uint8_t data);

/**
 * @brief  Print all ES8388 registers
 */
void es8388_read_all(void);

/**
 * @brief Configure ES8388 codec mode and I2S interface
 *
 * @param mode codec mode
 * @param iface I2S config
 *
 * @return
 *     - ESP_FAIL Parameter error
 *     - ESP_OK   Success
 */
esp_err_t es8388_config_i2s(audio_hal_codec_mode_t mode, audio_hal_codec_i2s_iface_t *iface);

/**
 * @brief Control ES8388 codec chip
 *
 * @param mode codec mode
 * @param ctrl_state start or stop decode or encode progress
 *
 * @return
 *     - ESP_FAIL Parameter error
 *     - ESP_OK   Success
 */
esp_err_t es8388_ctrl_state(audio_hal_codec_mode_t mode, audio_hal_ctrl_t ctrl_state);

/**
 * @brief Set ES8388 PA power
 *
 * @param enable true for enable PA power, false for disable PA power
 *
 * @return
 *     - ESP_ERR_INVALID_ARG
 *     - ESP_OK
 */
esp_err_t es8388_pa_power(bool enable);

#ifdef __cplusplus
}
#endif

#endif //__ES8388_H__
