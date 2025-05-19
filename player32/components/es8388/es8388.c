/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2018 <ESPRESSIF SYSTEMS (SHANGHAI) PTE LTD>
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

#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_check.h" // For ESP_RETURN_ON_ERROR macro

#include "driver/i2c.h"
#include "driver/i2s_std.h"

#include "es8388.h"
#include "esxxx_common.h"
#include "headphone_detect.h"

static const char *TAG = "ES8388_DRIVER";



// static i2c_bus_handle_t i2c_handle; - new interface uses the i2c_port
i2s_chan_handle_t g_i2s_tx_handle = NULL;
// i2s_chan_handle_t g_i2s_rx_handle = NULL;

// pins and such
#define ES8388_I2C_NUM  (I2C_NUM_0)
#define ES8388_I2C_SDA (GPIO_NUM_33)
#define ES8388_I2C_SCL (GPIO_NUM_32)
#define ES8388_I2C_FREQ_HZ (100000) // likely 400000 works too

#define ES8388_CODEC_I2S_PORT           (I2S_NUM_0)
#define ES8388_CODEC_BITS_PER_SAMPLE    (I2S_DATA_BIT_WIDTH_16BIT)
#define ES8388_CODEC_SAMPLE_RATE        (48000)
#define ES8388_RECORD_HARDWARE_AEC      (false)
#define ES8388_CODEC_CHANNEL_FMT        (I2S_CHANNEL_FMT_STEREO)


#define ES8388_PA_ENABLE_GPIO              (GPIO_NUM_21)
#define ES8388_BOARD_PA_GAIN             (10) /* Power amplifier gain defined by board (dB) */

// this is the ratio between the ESP system clock and the MCLK. It seems
// to depend on the configured sample rate. It seems common to set the value
// to 256, trying that, will go look up the ADF code if it doesn't work
#define ES8388_I2S_MCLK_MULTIPLE (256)


// MCLOCK - master; BCLK bit clock; WS Word slot select; Serial data in / out
#define ES8388_I2S_MCK (GPIO_NUM_0)
#define ES8388_I2S_BCK (GPIO_NUM_27)
#define ES8388_I2S_WS (GPIO_NUM_25)
#define ES8388_I2S_DATA_OUT (GPIO_NUM_26)
#define ES8388_I2S_DATA_IN (GPIO_NUM_35)


// get this sorted in a minute
// static codec_dac_volume_config_t *dac_vol_handle;

#define ES8388_DAC_VOL_CFG_DEFAULT() {                      \
    .max_dac_volume = 0,                                    \
    .min_dac_volume = -96,                                  \
    .board_pa_gain = BOARD_PA_GAIN,                         \
    .volume_accuracy = 0.5,                                 \
    .dac_vol_symbol = -1,                                   \
    .zero_volume_reg = 0,                                   \
    .reg_value = 0,                                         \
    .user_volume = 0,                                       \
    .offset_conv_volume = NULL,                             \
}

#define ES_ASSERT(a, format, b, ...) \
    if ((a) != 0) { \
        ESP_LOGE(TAG, format, ##__VA_ARGS__); \
        return b;\
    }

// audio_hal_func_t AUDIO_CODEC_ES8388_DEFAULT_HANDLE = {
//     .audio_codec_initialize = es8388_init,
//     .audio_codec_deinitialize = es8388_deinit,
//     .audio_codec_ctrl = es8388_ctrl_state,
//     .audio_codec_config_iface = es8388_config_i2s,
//     .audio_codec_set_mute = es8388_set_voice_mute,
//     .audio_codec_set_volume = es8388_set_voice_volume,
//     .audio_codec_get_volume = es8388_get_voice_volume,
//     .audio_codec_enable_pa = es8388_pa_power,
//     .audio_hal_lock = NULL,
//     .handle = NULL,
// };

static esp_err_t es_write_reg(uint8_t reg_add, uint8_t data)
{
    uint8_t buf[2] = {reg_add, data};
    return i2c_master_write_to_device(ES8388_I2C_NUM, ES8388_ADDR, buf, sizeof(buf), 1000 / portTICK_PERIOD_MS);
}

static esp_err_t es_read_reg(uint8_t reg_add, uint8_t *p_data)
{
    return i2c_master_write_read_device(ES8388_I2C_NUM, ES8388_ADDR, &reg_add, 
        sizeof(reg_add), p_data,sizeof(uint8_t), 1000 / portTICK_PERIOD_MS);
    // return i2c_master_write_to_device(ES8388_I2C_NUM, ES8388_ADDR, &reg_add, sizeof(reg_add), p_data, 1);
}

static esp_err_t es_i2c_init()
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = ES8388_I2C_SDA,
        .scl_io_num = ES8388_I2C_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = ES8388_I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(ES8388_I2C_NUM, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(ES8388_I2C_NUM, conf.mode,
                                       0 /*rx buf*/, 0 /* tx buf */, 0 /* intr_alloc_flags */) 
                   );

    // int res;
    // i2c_config_t es_i2c_cfg = {
    //     .mode = I2C_MODE_MASTER,
    //     .sda_pullup_en = GPIO_PULLUP_ENABLE,
    //     .scl_pullup_en = GPIO_PULLUP_ENABLE,
    //     .master.clk_speed = 100000
    // };
    // res = get_i2c_pins(I2C_NUM_0, &es_i2c_cfg);
    // ES_ASSERT(res, "getting i2c pins error", -1);
    // i2c_handle = i2c_bus_create(I2C_NUM_0, &es_i2c_cfg);
    // return res;

    return(ESP_OK);
}

void es8388_read_all()
{
    for (unsigned int i = 0; i < 50; i++) {
        uint8_t reg = 0;
        es_read_reg(i, &reg);
        ESP_LOGI(TAG, "%x: %x", i, reg);
    }
}

// does not seem used
// esp_err_t es8388_write_reg(uint8_t reg_add, uint8_t data)
// {
//     return es_write_reg( reg_add, data);
// }

/**
 * @brief Configure ES8388 ADC and DAC volume. Basicly you can consider this as ADC and DAC gain
 *
 * @param mode:             set ADC or DAC or all
 * @param volume:           -96 ~ 0              for example Es8388SetAdcDacVolume(ES_MODULE_ADC, 30, 6); means set ADC volume -30.5db
 * @param dot:              whether include 0.5. for example Es8388SetAdcDacVolume(ES_MODULE_ADC, 30, 4); means set ADC volume -30db
 *
 * @return
 *     - (-1) Parameter error
 *     - (0)   Success
 */
static int es8388_set_adc_dac_volume(int mode, int volume, int dot)
{
    esp_err_t res = ESP_OK;
    if ( volume < -96 || volume > 0 ) {
        ESP_LOGW(TAG, "Warning: volume < -96! or > 0!\n");
        if (volume < -96)
            volume = -96;
        else
            volume = 0;
    }
    dot = (dot >= 5 ? 1 : 0);
    volume = (-volume << 1) + dot;
    if (mode == ES_MODULE_ADC || mode == ES_MODULE_ADC_DAC) {
        res |= es_write_reg(ES8388_ADCCONTROL8, volume);
        res |= es_write_reg(ES8388_ADCCONTROL9, volume);  //ADC Right Volume=0db
    }
    if (mode == ES_MODULE_DAC || mode == ES_MODULE_ADC_DAC) {
        res |= es_write_reg(ES8388_LDACVOL, volume);
        res |= es_write_reg(ES8388_RDACVOL, volume);
    }
    return res;
}


/**
 * @brief Power Management
 *
 * @param mod:      if ES_POWER_CHIP, the whole chip including ADC and DAC is enabled
 * @param enable:   false to disable true to enable
 * 
 * This unit starts the chip in different modes. The modes defined can be:
 *   ES_MODULE_MIN = -1,
 *   ES_MODULE_ADC = 0x01,
 *   ES_MODULE_DAC = 0x02,
 *   ES_MODULE_ADC_DAC = 0x03,
 *   ES_MODULE_LINE = 0x04,
 *   ES_MODULE_MAX
 *
 * @return
 *     - (-1)  Error
 *     - (0)   Success
 */
esp_err_t es8388_start(es_module_t mode)
{
    esp_err_t res = ESP_OK;
    uint8_t prev_data = 0, data = 0;
    es_read_reg(ES8388_DACCONTROL21, &prev_data);
    if (mode == ES_MODULE_LINE) {
        res |= es_write_reg(ES8388_DACCONTROL16, 0x09); // 0x00 audio on LIN1&RIN1,  0x09 LIN2&RIN2 by pass enable
        res |= es_write_reg(ES8388_DACCONTROL17, 0x50); // left DAC to left mixer enable  and  LIN signal to left mixer enable 0db  : bupass enable
        res |= es_write_reg(ES8388_DACCONTROL20, 0x50); // right DAC to right mixer enable  and  LIN signal to right mixer enable 0db : bupass enable
        res |= es_write_reg(ES8388_DACCONTROL21, 0xC0); //enable adc
    } else {
        res |= es_write_reg(ES8388_DACCONTROL21, 0x80);   //enable dac
    }
    es_read_reg(ES8388_DACCONTROL21, &data);
    if (prev_data != data) {
        res |= es_write_reg(ES8388_CHIPPOWER, 0xF0);   //start state machine
        // res |= es_write_reg(ES8388_CONTROL1, 0x16);
        // res |= es_write_reg(ES8388_CONTROL2, 0x50);
        res |= es_write_reg(ES8388_CHIPPOWER, 0x00);   //start state machine
    }
    if (mode == ES_MODULE_ADC || mode == ES_MODULE_ADC_DAC || mode == ES_MODULE_LINE) {
        res |= es_write_reg(ES8388_ADCPOWER, 0x00);   //power up adc and line in
    }
    if (mode == ES_MODULE_DAC || mode == ES_MODULE_ADC_DAC || mode == ES_MODULE_LINE) {
        res |= es_write_reg(ES8388_DACPOWER, 0x3c);   //power up dac and line out
        res |= es8388_set_mute(false);
        ESP_LOGD(TAG, "es8388_start default is mode:%d", mode);
    }

    return res;
}

/**
 * @brief Power Management
 *
 * @param mod:      if ES_POWER_CHIP, the whole chip including ADC and DAC is enabled
 * @param enable:   false to disable true to enable
 *
 * @return
 *     - (-1)  Error
 *     - (0)   Success
 */
esp_err_t es8388_stop(es_module_t mode)
{
    esp_err_t res = ESP_OK;
    if (mode == ES_MODULE_LINE) {
        res |= es_write_reg( ES8388_DACCONTROL21, 0x80); //enable dac
        res |= es_write_reg( ES8388_DACCONTROL16, 0x00); // 0x00 audio on LIN1&RIN1,  0x09 LIN2&RIN2
        res |= es_write_reg( ES8388_DACCONTROL17, 0x90); // only left DAC to left mixer enable 0db
        res |= es_write_reg( ES8388_DACCONTROL20, 0x90); // only right DAC to right mixer enable 0db
        return res;
    }
    if (mode == ES_MODULE_DAC || mode == ES_MODULE_ADC_DAC) {
        res |= es_write_reg( ES8388_DACPOWER, 0x00);
        res |= es8388_set_mute(true); //res |= Es8388SetAdcDacVolume(ES_MODULE_DAC, -96, 5);      // 0db
        //res |= es_write_reg( ES8388_DACPOWER, 0xC0);  //power down dac and line out
    }
    if (mode == ES_MODULE_ADC || mode == ES_MODULE_ADC_DAC) {
        //res |= Es8388SetAdcDacVolume(ES_MODULE_ADC, -96, 5);      // 0db
        res |= es_write_reg( ES8388_ADCPOWER, 0xFF);  //power down adc and line in
    }
    if (mode == ES_MODULE_ADC_DAC) {
        res |= es_write_reg( ES8388_DACCONTROL21, 0x9C);  //disable mclk
//        res |= es_write_reg( ES8388_CONTROL1, 0x00);
//        res |= es_write_reg( ES8388_CONTROL2, 0x58);
//        res |= es_write_reg( ES8388_CHIPPOWER, 0xF3);  //stop state machine
    }

    return res;
}


/**
 * @brief Config I2s clock in MSATER mode
 *
 * @param cfg.sclkDiv:      generate SCLK by dividing MCLK in MSATER mode
 * @param cfg.lclkDiv:      generate LCLK by dividing MCLK in MSATER mode
 *
 * @return
 *     - (-1)  Error
 *     - (0)   Success
 */
esp_err_t es8388_i2s_config_clock(es_i2s_clock_t cfg)
{
    esp_err_t res = ESP_OK;
    res |= es_write_reg( ES8388_MASTERMODE, cfg.sclk_div);
    res |= es_write_reg( ES8388_ADCCONTROL5, cfg.lclk_div);  //ADCFsMode,singel SPEED,RATIO=256
    res |= es_write_reg( ES8388_DACCONTROL2, cfg.lclk_div);  //ADCFsMode,singel SPEED,RATIO=256
    return res;
}


/**
 * @brief Initialize I2S peripheral for ES8388 communication
 *
 * CONFIGURED FOR OUTPUT ONLY, comment in the RX structure for receiving from mics
 * 
 * @return esp_err_t ESP_OK on success, error code otherwise.
 */

static esp_err_t es_i2s_init(void) {
    //esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Initializing I2S for ES8388...");

// Note. At 44.1khz, this works out to a dma buffer time of 22.68us.
// this means about 5.44us between a full buffer and an empty buffer.

    // 1. Configure I2S Channel (Common settings)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(ES8388_CODEC_I2S_PORT, I2S_ROLE_MASTER);
    // Allocate DMA buffers - Adjust sizes if needed
    chan_cfg.dma_desc_num = 6; // Number of DMA descriptors
    chan_cfg.dma_frame_num = 240; // Number of frames (samples) per descriptor
    chan_cfg.auto_clear = true; // Auto clear TX buffer on underflow

    ESP_LOGI(TAG, "Allocating I2S channels...");
    // Allocate TX channel
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &g_i2s_tx_handle, NULL), TAG, "Failed to create TX channel");
    // Allocate RX channel
    // BB: skip because this is for mic, not using yet
    // ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, NULL, &g_i2s_rx_handle), TAG, "Failed to create RX channel");


    // 2. Configure I2S Standard Mode (Specific to standard I2S protocol)
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(ES8388_CODEC_SAMPLE_RATE),
        // --- Revised slot_cfg section ---
        .slot_cfg = {
            .data_bit_width = ES8388_CODEC_BITS_PER_SAMPLE,
            // Set total bits per slot. AUTO usually sets it equal to data_bit_width.
            // For standard I2S (Philips), slot width often equals data width (e.g., 16-bit data in a 16-bit slot).
            // Can be explicitly set (e.g., I2S_SLOT_BIT_WIDTH_16BIT).
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO, // Stereo mode
            // Select which slots are active. I2S_STD_SLOT_BOTH uses typical Left/Right channels.
            .slot_mask = I2S_STD_SLOT_BOTH,
                // Default values below are typically correct for standard I2S (Philips)
            .ws_width = 0,         // Auto = slot_bit_width
            .ws_pol = false,       // WS low for left channel (standard I2S)
            .bit_shift = false,    // Data starts one clock after WS edge (standard I2S)
            .msb_right = false   // // Data is MSB justified, not right justified (standard I2S)
        },
        // --- End of revised slot_cfg section ---
        .gpio_cfg = {
            .mclk = ES8388_I2S_MCK,
            .bclk = ES8388_I2S_BCK,
            .ws = ES8388_I2S_WS,
            .dout = ES8388_I2S_DATA_OUT,
            .din = -1, // Use -1 if RX is not used - or DATA_IN if you want the mic

            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    // Adjust clock config for MCLK output
    std_cfg.clk_cfg.mclk_multiple = ES8388_I2S_MCLK_MULTIPLE;
    std_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT; // Or I2S_CLK_SRC_APLL for potentially better quality

    // Adjust slot config (Though default Philips is likely correct)
    // Make sure data bit width and slot bit width match your needs
    std_cfg.slot_cfg.data_bit_width = ES8388_CODEC_BITS_PER_SAMPLE;
    std_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO; // Auto = same as data bit width
    // std_cfg.slot_cfg.ws_width = I2S_BITS_PER_SAMPLE; // Usually same as bits per sample for standard I2S

    ESP_LOGI(TAG, "Initializing standard mode for TX channel...");
    // Initialize TX channel in standard mode
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(g_i2s_tx_handle, &std_cfg), TAG, "Failed to init TX channel");

    ESP_LOGI(TAG, "Initializing standard mode for RX channel...");
    // Initialize RX channel in standard mode
    // Note: We re-use std_cfg, but GPIO dout/din are handled internally based on tx/rx handle
    // ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(g_i2s_rx_handle, &std_cfg), TAG, "Failed to init RX channel");


    // 3. Enable I2S Channels (Start clocks)
    ESP_LOGI(TAG, "Enabling I2S channels...");
    // Must enable TX before RX if using shared MCLK/BCLK/WS pins with internal loopback (not typical for external codec)
    // Or if driving MCLK from TX channel (common setup)
    ESP_RETURN_ON_ERROR(i2s_channel_enable(g_i2s_tx_handle), TAG, "Failed to enable TX channel");
    // ESP_RETURN_ON_ERROR(i2s_channel_enable(g_i2s_rx_handle), TAG, "Failed to enable RX channel");


    ESP_LOGI(TAG, "I2S initialization complete.");
    return ESP_OK;
}

esp_err_t es8388_deinit(void)
{
    int res = 0;
    res = es_write_reg( ES8388_CHIPPOWER, 0xFF);  //reset and stop es8388
    i2c_driver_delete(ES8388_I2C_NUM);
    headphone_detect_deinit();
// pretty sure this is a thing from ADF
//    audio_codec_volume_deinit(dac_vol_handle);
    return res;
}


/**
 * @return
 *     - (-1)  Error
 *     - (0)   Success
 */
esp_err_t es8388_init(es_codec_config_t *cfg)
{
    int res = 0;

    headphone_detect_init();

    res = es_i2c_init(); // ESP32 in master mode

    res |= es_write_reg( ES8388_DACCONTROL3, 0x04);  // 0x04 mute/0x00 unmute&ramp;DAC unmute and  disabled digital volume control soft ramp
    /* Chip Control and Power Management */
    res |= es_write_reg( ES8388_CONTROL2, 0x50);
    res |= es_write_reg( ES8388_CHIPPOWER, 0x00); //normal all and power up all

    // Disable the internal DLL to improve 8K sample rate
    res |= es_write_reg( 0x35, 0xA0);
    res |= es_write_reg( 0x37, 0xD0);
    res |= es_write_reg( 0x39, 0xD0);

    res |= es_write_reg( ES8388_MASTERMODE, cfg->i2s_iface.mode); //CODEC IN I2S SLAVE MODE

    /* dac */
    res |= es_write_reg( ES8388_DACPOWER, 0xC0);  //disable DAC and disable Lout/Rout/1/2
    res |= es_write_reg( ES8388_CONTROL1, 0x12);  //Enfr=0,Play&Record Mode,(0x17-both of mic&paly)
//    res |= es_write_reg( ES8388_CONTROL2, 0);  //LPVrefBuf=0,Pdn_ana=0
    res |= es_write_reg( ES8388_DACCONTROL1, 0x18); //1a 0x18:16bit iis , 0x00:24
    res |= es_write_reg( ES8388_DACCONTROL2, 0x02);  //DACFsMode,SINGLE SPEED; DACFsRatio,256
    res |= es_write_reg( ES8388_DACCONTROL16, 0x00); // 0x00 audio on LIN1&RIN1,  0x09 LIN2&RIN2
    res |= es_write_reg( ES8388_DACCONTROL17, 0x90); // only left DAC to left mixer enable 0db
    res |= es_write_reg( ES8388_DACCONTROL20, 0x90); // only right DAC to right mixer enable 0db
    res |= es_write_reg( ES8388_DACCONTROL21, 0x80); // set internal ADC and DAC use the same LRCK clock, ADC LRCK as internal LRCK
    res |= es_write_reg( ES8388_DACCONTROL23, 0x00); // vroi=0

    res |= es_write_reg( ES8388_DACCONTROL24, 0x1E); // Set L1 R1 L2 R2 volume. 0x00: -30dB, 0x1E: 0dB, 0x21: 3dB
    res |= es_write_reg( ES8388_DACCONTROL25, 0x1E);
    res |= es_write_reg( ES8388_DACCONTROL26, 0);
    res |= es_write_reg( ES8388_DACCONTROL27, 0);
    // res |= es8388_set_adc_dac_volume(ES_MODULE_DAC, 0, 0);       // 0db

    res |= es_write_reg( ES8388_DACPOWER, cfg->dac_output);  //0x3c Enable DAC and Enable Lout/Rout/1/2
    /* adc */
    res |= es_write_reg( ES8388_ADCPOWER, 0xFF);
    res |= es_write_reg( ES8388_ADCCONTROL1, 0xbb); // MIC Left and Right channel PGA gain
    res |= es_write_reg( ES8388_ADCCONTROL2, cfg->adc_input);  //0x00 LINSEL & RINSEL, LIN1/RIN1 as ADC Input; DSSEL,use one DS Reg11; DSR, LINPUT1-RINPUT1
    res |= es_write_reg( ES8388_ADCCONTROL3, 0x20); // BB - consider not setting flash to low power?
    res |= es_write_reg( ES8388_ADCCONTROL4, 0x0c); // 16 Bits length and I2S serial audio data format
    res |= es_write_reg( ES8388_ADCCONTROL5, 0x02);  //ADCFsMode,singel SPEED,RATIO=256
    //ALC for Microphone
    res |= es8388_set_adc_dac_volume(ES_MODULE_ADC, 0, 0);      // 0db
    res |= es_write_reg( ES8388_ADCPOWER, 0x09);    // Power on ADC, enable LIN&RIN, power off MICBIAS, and set int1lp to low power mode
    
    /* es8388 PA gpio_config */
    gpio_config_t  io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(ES8388_PA_ENABLE_GPIO), // same as a 1 << 
        .pull_down_en = 0, // disable
        .pull_up_en = 0, // disable
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
    /* enable es8388 PA */
    es8388_pa_power(true);

    es_i2s_init();

    // This call into ADF probably needs to be replaced by some other default volume call
    // codec_dac_volume_config_t vol_cfg = ES8388_DAC_VOL_CFG_DEFAULT();
    // dac_vol_handle = audio_codec_volume_init(&vol_cfg);
    // ESP_LOGI(TAG, "init,out:%02x, in:%02x", cfg->dac_output, cfg->adc_input);
    return res;
}

/**
 * @brief Configure ES8388 I2S format
 *
 * @param mode:           set ADC or DAC or all
 * @param bitPerSample:   see Es8388I2sFmt
 *
 * @return
 *     - (-1) Error
 *     - (0)  Success
 */
esp_err_t es8388_config_fmt(es_module_t mode, es_i2s_fmt_t fmt)
{
    esp_err_t res = ESP_OK;
    uint8_t reg = 0;
    if (mode == ES_MODULE_ADC || mode == ES_MODULE_ADC_DAC) {
        res = es_read_reg(ES8388_ADCCONTROL4, &reg);
        reg = reg & 0xfc;
        res |= es_write_reg( ES8388_ADCCONTROL4, reg | fmt);
    }
    if (mode == ES_MODULE_DAC || mode == ES_MODULE_ADC_DAC) {
        res = es_read_reg(ES8388_DACCONTROL1, &reg);
        reg = reg & 0xf9;
        res |= es_write_reg( ES8388_DACCONTROL1, reg | (fmt << 1));
    }
    return res;
}

/**
 * @brief Set voice volume
 *
 * @note Register values. 0xC0: -96 dB, 0x64: -50 dB, 0x00: 0 dB
 * @note Accuracy of gain is 0.5 dB
 *
 * @param volume: voice volume (0~100)
 *
 * @return
 *     - ESP_OK
 *     - ESP_FAIL
 */

// the best way to convert volume to the ES8388's values is with a log.
// the best way to calculate a log on an ESP32 is through a lookup table.
// This gives low = 0 31, and the high end the same way, according to
// a log table.

 static const uint8_t volume_table[100] = {
    31,30,30,29,29,28,28,27,27,26,26,25,25,24,24,23,23,22,22,21,
    21,20,20,19,19,18,18,17,17,16,16,15,15,14,14,13,13,12,12,11,
    11,10,10, 9, 9, 8, 8, 7, 7, 6, 6, 5, 5, 4, 4, 3, 3, 2, 2, 2,
     2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
     1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
   };
   
static int g_user_volume = -1;

esp_err_t es8388_set_volume(int volume)
{
    // Original
//    esp_err_t res = ESP_OK;
//    uint8_t reg = 0;
//    reg = audio_codec_get_dac_reg_value(dac_vol_handle, volume);
//    res |= es_write_reg( ES8388_DACCONTROL5, reg);
//    res |= es_write_reg( ES8388_DACCONTROL4, reg);
//    ESP_LOGD(TAG, "Set volume:%.2d reg_value:0x%.2x dB:%.1f", (int)dac_vol_handle->user_volume, reg,
//            audio_codec_cal_dac_volume(dac_vol_handle));
//    return res;

    esp_err_t res = ESP_OK; 
    if (volume < 0) volume = 0; else if (volume > 100) volume = 100;
    uint8_t vol_value = volume_table[volume];

    res = es_write_reg( ES8388_LDACVOL, vol_value); // LDACVOL  0..-96db  in 0.5steps (0=loud, 192=silent)
    res |= es_write_reg( ES8388_RDACVOL, vol_value); // RDACVOL 0..-96db  in 0.5steps (0=loud, 192=silent)

    g_user_volume = volume;

    // there is difference of opinion about which volume system to use for this kind of volume
    // control. There are these settings as well. The advice I have is to set these to reasonable
    // high values, and use the xADCVOL as the correct ones. These will need 
    // res |= es_write_reg( ES8388_LOUT1VOL, vol_value);  // LOUT1 volume 0..33 dB - headphone
    // res |= es_write_reg( ES8388_ROUT1VOL, vol_value);  // ROUT1 volume 0..33 dB - headphone
    // res |= es_write_reg( ES8388_LOUT2VOL, vol_value);  // LOUT2 volume 0..33 dB - power amp
    // res |= es_write_reg( ES8388_ROUT2VOL, vol_value);  // ROUT2 volume 0..33 dB - power amp

    if (res == ESP_OK) {
        ESP_LOGI(TAG, "Success: Set volume:%.2d dB:%.1f", (int)volume, 
                -1.0 /* dbd */);
    } else {
        ESP_LOGE(TAG, "FAILURE: Set volume:%.2d dB:%.1f", (int)volume, 
                -1.0 /* dbd */);        
    }
    return res;

}

esp_err_t es8388_get_volume(int *volume)
{
    esp_err_t res = ESP_OK;
    *volume = g_user_volume;
    // It is safer to read from whatever you're using to make sure the volume is the same,
    // and only return the saved value after you've validated.
    // uint8_t reg = 0;
    // res = es_read_reg(ES8388_DACCONTROL4, &reg);
    // if (res == ESP_FAIL) {
    //     *volume = 0;
    // } else {
    //     if (reg == dac_vol_handle->reg_value) {
    //         *volume = dac_vol_handle->user_volume;
    //     } else {
    //         *volume = 0;
    //         res = ESP_FAIL;
    //     }
    // }
    // ESP_LOGI(TAG, "Get volume:%.2d reg_value:0x%.2x", *volume);
    ESP_LOGI(TAG, "Get volume:%.2d", *volume);
    return res;
}

/**
 * @brief Configure ES8388 data sample bits
 *
 * @param mode:             set ADC or DAC or all
 * @param bitPerSample:   see BitsLength
 *
 * @return
 *     - (-1) Parameter error
 *     - (0)   Success
 */
esp_err_t es8388_set_bits_per_sample(es_module_t mode, es_bits_length_t bits_length)
{
    esp_err_t res = ESP_OK;
    uint8_t reg = 0;
    int bits = (int)bits_length;

    if (mode == ES_MODULE_ADC || mode == ES_MODULE_ADC_DAC) {
        res = es_read_reg(ES8388_ADCCONTROL4, &reg);
        reg = reg & 0xe3;
        res |=  es_write_reg( ES8388_ADCCONTROL4, reg | (bits << 2));
    }
    if (mode == ES_MODULE_DAC || mode == ES_MODULE_ADC_DAC) {
        res = es_read_reg(ES8388_DACCONTROL1, &reg);
        reg = reg & 0xc7;
        res |= es_write_reg( ES8388_DACCONTROL1, reg | (bits << 3));
    }
    return res;
}

/**
 * @brief Configure ES8388 DAC mute or not. Basically you can use this function to mute the output or unmute
 *
 * @param enable: enable or disable
 *
 * @return
 *     - (-1) Parameter error
 *     - (0)   Success
 */
esp_err_t es8388_set_mute(bool enable)
{
    esp_err_t res = ESP_OK;
    uint8_t reg = 0;
    res = es_read_reg(ES8388_DACCONTROL3, &reg);
    reg = reg & 0xFB; 
    res |= es_write_reg( ES8388_DACCONTROL3, reg | (((int)enable) << 2));
    return res;
}

esp_err_t es8388_get_mute(void)
{
    esp_err_t res = ESP_OK;
    uint8_t reg = 0;
    res = es_read_reg(ES8388_DACCONTROL3, &reg);
    if (res == ESP_OK) {
        reg = (reg & 0x04) >> 2;
    }
    return res == ESP_OK ? reg : res;
}

/**
 * @param gain: Config DAC Output
 *
 * @return
 *     - (-1) Parameter error
 *     - (0)   Success
 */
esp_err_t es8388_config_dac_output(es_dac_output_t output)
{
    esp_err_t res;
    uint8_t reg = 0;
    res = es_read_reg(ES8388_DACPOWER, &reg);
    reg = reg & 0xc3;
    res |= es_write_reg( ES8388_DACPOWER, reg | output);
    return res;
}

/**
 * @param gain: Config ADC input
 *
 * @return
 *     - (-1) Parameter error
 *     - (0)   Success
 */
esp_err_t es8388_config_adc_input(es_adc_input_t input)
{
    esp_err_t res;
    uint8_t reg = 0;
    res = es_read_reg(ES8388_ADCCONTROL2, &reg);
    reg = reg & 0x0f;
    res |= es_write_reg( ES8388_ADCCONTROL2, reg | input);
    return res;
}

/**
 * @param gain: see es_mic_gain_t
 *
 * @return
 *     - (-1) Parameter error
 *     - (0)   Success
 */
esp_err_t es8388_set_mic_gain(es_mic_gain_t gain)
{
    esp_err_t res, gain_n;
    gain_n = (int)gain / 3;
    gain_n = (gain_n << 4) + gain_n;
    res = es_write_reg( ES8388_ADCCONTROL1, gain_n); //MIC PGA
    return res;
}

int es8388_ctrl_state(es_codec_mode_t mode, es_ctrl_t ctrl_state)
{
    int res = 0;
    int es_mode_t = 0;
    switch (mode) {
        case ES_CODEC_MODE_ENCODE:
            es_mode_t  = ES_MODULE_ADC;
            break;
        case ES_CODEC_MODE_LINE_IN:
            es_mode_t  = ES_MODULE_LINE;
            break;
        case ES_CODEC_MODE_DECODE:
            es_mode_t  = ES_MODULE_DAC;
            break;
        case ES_CODEC_MODE_BOTH:
            es_mode_t  = ES_MODULE_ADC_DAC;
            break;
        default:
            es_mode_t = ES_MODULE_DAC;
            ESP_LOGW(TAG, "Codec mode not support, default is decode mode");
            break;
    }
    if (ES_CTRL_STOP == ctrl_state) {
        res = es8388_stop(mode);
    } else if (ES_CTRL_START == ctrl_state)  {
        res = es8388_start(mode);
        ESP_LOGD(TAG, "start default is decode mode:%d", es_mode_t);
    } else {
        ESP_LOGW(TAG, "ctrl unknown state");
    }
    return res;
}

esp_err_t es8388_config_i2s(es_codec_mode_t mode, es_codec_i2s_iface_t *iface)
{
    esp_err_t res = ESP_OK;
    int tmp = 0;
    res |= es8388_config_fmt(ES_MODULE_ADC_DAC, iface->fmt);
    if (iface->bits == ES_BIT_LENGTH_16BITS) {
        tmp = BIT_LENGTH_16BITS;
    } else if (iface->bits == ES_BIT_LENGTH_24BITS) {
        tmp = BIT_LENGTH_24BITS;
    } else {
        tmp = BIT_LENGTH_32BITS;
    }
    res |= es8388_set_bits_per_sample(ES_MODULE_ADC_DAC, tmp);
    return res;
}

esp_err_t es8388_pa_power(bool enable)
{
    esp_err_t res = ESP_OK;
    if (enable) {
        res = gpio_set_level(ES8388_PA_ENABLE_GPIO, 1);
    } else {
        res = gpio_set_level(ES8388_PA_ENABLE_GPIO, 0);
    }
    return res;
}

/*
* Writer functions. 
* 
* This blocks until the bytes are written.
* Note that due to buffering, we can't really delay here. The DMA buffer is only about 5ms deep, 
* and the clock granularity

* return values for i2s_channel_write
* ESP_OK Write successfully
* ESP_ERR_INVALID_ARG NULL pointer or this handle is not TX handle
* ESP_ERR_TIMEOUT Writing timeout, no writing event received from ISR within ticks_to_wait
* ESP_ERR_INVALID_STATE I2S is not ready to write
*
* Interesting point on the timeout. MS here converts to ticks, and ticks are at 100hz, therefore
* the underlying granularity is 10ms, which is significantly deeper than the DMA depth. 
* it also means calling with less than 10ms will mean the function is effectively non-blocking,
* which could cause problems for other unit.
*/

// Unified write function
esp_err_t es8388_write(const void *buffer, size_t bytes_to_write, size_t *bytes_written_r) {

    size_t bytes_written = 0;
    uint8_t *o_buf = (uint8_t *) buffer;
    // int64_t start_us = esp_timer_get_time();
    while (bytes_written < bytes_to_write) {
        size_t b_w = 0;
        esp_err_t ret = i2s_channel_write(g_i2s_tx_handle, o_buf + bytes_written, bytes_to_write - bytes_written, &b_w, portMAX_DELAY);
        bytes_written += b_w;
        if (ret != ESP_OK && ret != ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "i2s_channel_write: returned failure code returning error %d", ret);
            *bytes_written_r = bytes_written;
            return (ret);
        }
    }
    // ESP_LOGI(TAG, "es8388 write: took %lld us ", esp_timer_get_time() - start_us );

    *bytes_written_r = bytes_written;
    return(ESP_OK);

    // while (1) {
    //     // i2s_tx_chan was created in init_i2s_std()
    //     i2s_channel_write(i2s_tx_chan, audio_buf, total_bytes, &bytes_written, portMAX_DELAY);
    //     // In production code, handle any potential write errors or break conditions
    // }



}

