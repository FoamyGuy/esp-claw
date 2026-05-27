/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 Anne Barela for Adafruit Industries
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "esp_io_expander.h"
#include "esp_io_expander_tca9554.h"

#include "esp_board_manager_includes.h"
#include "gen_board_device_custom.h"
#include "../../managed_components/espressif__esp_board_manager/devices/dev_display_lcd/dev_display_lcd.h"

static const char *TAG = "QUALIA_ESP32S3_RGB666_SMALL_ROUND";

/* PCA9554A pin assignments per Adafruit pins_arduino.h */
#define PCA_BIT_CLK        0    /* TFT_SCK         (output) */
#define PCA_BIT_CS         1    /* TFT_CS          (output) */
#define PCA_BIT_RESET      2    /* TFT_RESET       (output) */
#define PCA_BIT_TOUCH_IRQ  3    /* CPT_IRQ         (input)  */
#define PCA_BIT_BACKLIGHT  4    /* TFT_BACKLIGHT   (output, HIGH = on) */
#define PCA_BIT_BTN_UP     5    /* BUTTON_UP       (input)  */
#define PCA_BIT_BTN_DN     6    /* BUTTON_DOWN     (input)  */
#define PCA_BIT_MOSI       7    /* TFT_MOSI        (output) */

/* ST7701S init sequence for TL021WVC02CT 2.1" round 480x480.
 * Source: https://cdn-shop.adafruit.com/product-files/5792/TL021WVC-B1323+SPI+Init+Code.txt
 *
 * Format: flat byte stream, parsed as a sequence of commands. Each command is
 * encoded as { CMD_OP, len, cmd_byte, data_bytes... } where len is the number
 * of data bytes (0..n). A length byte of 0xFF is reserved as DELAY_OP, where
 * the next byte is the delay in 10ms units.
 */
#define CMD_OP   0x00
#define DELAY_OP 0xFF

static const uint8_t st7701_init[] = {
    /* Page 10 — main display config */
    CMD_OP, 5, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x10,
    CMD_OP, 2, 0xC0, 0x3B, 0x00,
    CMD_OP, 2, 0xC1, 0x0B, 0x02,
    CMD_OP, 2, 0xC2, 0x00, 0x02,
    CMD_OP, 1, 0xCC, 0x10,
    CMD_OP, 1, 0xCD, 0x08,
    /* Positive gamma (B0) */
    CMD_OP, 16, 0xB0, 0x02, 0x13, 0x1B, 0x0D, 0x10, 0x05, 0x08, 0x07,
                       0x07, 0x24, 0x04, 0x11, 0x0E, 0x2C, 0x33, 0x1D,
    /* Negative gamma (B1) */
    CMD_OP, 16, 0xB1, 0x05, 0x13, 0x1B, 0x0D, 0x11, 0x05, 0x08, 0x07,
                       0x07, 0x24, 0x04, 0x11, 0x0E, 0x2C, 0x33, 0x1D,

    /* Page 11 — power and timing config */
    CMD_OP, 5, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x11,
    CMD_OP, 1, 0xB0, 0x5D,
    CMD_OP, 1, 0xB1, 0x43,
    CMD_OP, 1, 0xB2, 0x81,
    CMD_OP, 1, 0xB3, 0x80,
    CMD_OP, 1, 0xB5, 0x43,
    CMD_OP, 1, 0xB7, 0x85,
    CMD_OP, 1, 0xB8, 0x20,
    CMD_OP, 1, 0xC1, 0x78,
    CMD_OP, 1, 0xC2, 0x78,
    CMD_OP, 1, 0xD0, 0x88,
    CMD_OP, 3, 0xE0, 0x00, 0x00, 0x02,
    CMD_OP, 11, 0xE1, 0x03, 0xA0, 0x00, 0x00, 0x04, 0xA0, 0x00,
                       0x00, 0x00, 0x20, 0x20,
    CMD_OP, 13, 0xE2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x00,
    CMD_OP, 4, 0xE3, 0x00, 0x00, 0x11, 0x00,
    CMD_OP, 2, 0xE4, 0x22, 0x00,
    CMD_OP, 16, 0xE5, 0x05, 0xEC, 0xA0, 0xA0, 0x07, 0xEE, 0xA0, 0xA0,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    CMD_OP, 4, 0xE6, 0x00, 0x00, 0x11, 0x00,
    CMD_OP, 2, 0xE7, 0x22, 0x00,
    CMD_OP, 16, 0xE8, 0x06, 0xED, 0xA0, 0xA0, 0x08, 0xEF, 0xA0, 0xA0,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    CMD_OP, 7, 0xEB, 0x00, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00,
    CMD_OP, 16, 0xED, 0xFF, 0xFF, 0xFF, 0xBA, 0x0A, 0xBF, 0x45, 0xFF,
                       0xFF, 0x54, 0xFB, 0xA0, 0xAB, 0xFF, 0xFF, 0xFF,
    CMD_OP, 6, 0xEF, 0x10, 0x0D, 0x04, 0x08, 0x3F, 0x1F,

    /* Page 13 — vendor-specific tweak */
    CMD_OP, 5, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x13,
    CMD_OP, 1, 0xEF, 0x08,

    /* Page 0 — final user config and turn on */
    CMD_OP, 5, 0xFF, 0x77, 0x01, 0x00, 0x00, 0x00,
    CMD_OP, 1, 0x36, 0x00,        /* MADCTL — memory data access control */
    CMD_OP, 1, 0x3A, 0x60,        /* COLMOD — pixel format (RGB-666) */
    CMD_OP, 0, 0x11,              /* SLPOUT (sleep out) */
    DELAY_OP, 100,                /* 100 ms wait per panel spec */
    CMD_OP, 0, 0x29,              /* DISPON (display on) */
    DELAY_OP, 50,                 /* 50 ms settle */
};

/* Send one bit on the bit-banged 3-wire SPI port via PCA9554A. */
static esp_err_t spi_send_bit(esp_io_expander_handle_t exp, int bit)
{
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(exp, BIT(PCA_BIT_CLK), 0),
                        TAG, "CLK low failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(exp, BIT(PCA_BIT_MOSI), bit ? 1 : 0),
                        TAG, "MOSI failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(exp, BIT(PCA_BIT_CLK), 1),
                        TAG, "CLK high failed");
    return ESP_OK;
}

/* Send one 9-bit word: D/CX bit, then 8 data bits MSB first. */
static esp_err_t spi_send_word9(esp_io_expander_handle_t exp, bool is_data, uint8_t byte)
{
    ESP_RETURN_ON_ERROR(spi_send_bit(exp, is_data ? 1 : 0), TAG, "DC bit failed");
    for (int i = 7; i >= 0; i--) {
        ESP_RETURN_ON_ERROR(spi_send_bit(exp, (byte >> i) & 1), TAG, "data bit failed");
    }
    return ESP_OK;
}

/* Send a single-command, multi-parameter ST7701S frame: CS asserted across
 * cmd byte (D/C=0) and all parameter bytes (D/C=1), then CS released. */
static esp_err_t st7701_write(esp_io_expander_handle_t exp,
                              uint8_t cmd, const uint8_t *params, size_t nparams)
{
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(exp, BIT(PCA_BIT_CS), 0),
                        TAG, "CS low failed");
    ESP_RETURN_ON_ERROR(spi_send_word9(exp, false, cmd), TAG, "cmd failed");
    for (size_t i = 0; i < nparams; i++) {
        ESP_RETURN_ON_ERROR(spi_send_word9(exp, true, params[i]), TAG, "param failed");
    }
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(exp, BIT(PCA_BIT_CS), 1),
                        TAG, "CS high failed");
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(exp, BIT(PCA_BIT_CLK), 0),
                        TAG, "CLK park failed");
    return ESP_OK;
}

/* Walk the init blob, dispatching CMD_OP and DELAY_OP entries. */
static esp_err_t st7701_run_init(esp_io_expander_handle_t exp)
{
    size_t i = 0;
    int cmd_count = 0;

    while (i < sizeof(st7701_init)) {
        uint8_t op = st7701_init[i++];
        if (op == DELAY_OP) {
            /* Delay is encoded as raw milliseconds (max 255 ms — sufficient for
             * SLPOUT 100ms and DISPON 50ms; longer waits are split into chunks
             * or use vTaskDelay outside this walker). */
            uint8_t ms = st7701_init[i++];
            vTaskDelay(pdMS_TO_TICKS(ms));
            continue;
        }
        /* CMD_OP: next is param count, then cmd byte, then params */
        uint8_t nparams = st7701_init[i++];
        uint8_t cmd = st7701_init[i++];
        ESP_RETURN_ON_ERROR(st7701_write(exp, cmd, &st7701_init[i], nparams),
                            TAG, "ST7701 cmd 0x%02X failed", cmd);
        i += nparams;
        cmd_count++;
    }

    ESP_LOGI(TAG, "ST7701S init complete: %d commands", cmd_count);
    return ESP_OK;
}

/* Factory called by gpio_expander framework code after PCA9554A is up. */
esp_err_t io_expander_factory_entry_t(i2c_master_bus_handle_t i2c_handle,
                                      const uint16_t dev_addr,
                                      esp_io_expander_handle_t *handle_ret)
{
    /* PCA9554A is register-compatible with TCA9554A — see espressif/esp-bsp#335. */
    esp_err_t ret = esp_io_expander_new_i2c_tca9554(i2c_handle, dev_addr, handle_ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PCA9554A IO expander: %s", esp_err_to_name(ret));
        return ret;
    }

    /* CS high, RESET low, CLK/MOSI low. Panel held in reset until display init runs. */
    esp_io_expander_set_level(*handle_ret, BIT(PCA_BIT_CS),        1);
    esp_io_expander_set_level(*handle_ret, BIT(PCA_BIT_RESET),     0);
    esp_io_expander_set_level(*handle_ret, BIT(PCA_BIT_CLK),       0);
    esp_io_expander_set_level(*handle_ret, BIT(PCA_BIT_MOSI),      0);
    /* Backlight: HIGH = on. TPS61169 enable. Required for any visible output. */
    esp_io_expander_set_level(*handle_ret, BIT(PCA_BIT_BACKLIGHT), 1);

    return ESP_OK;
}

static esp_err_t qualia_panel_reset_pulse(esp_io_expander_handle_t exp)
{
    /* ST7701 spec: tRW (reset pulse) min 10us; tRT (reset cancel) 5-120ms. */
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(exp, BIT(PCA_BIT_RESET), 0),
                        TAG, "Reset assert failed");
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(esp_io_expander_set_level(exp, BIT(PCA_BIT_RESET), 1),
                        TAG, "Reset release failed");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

static int qualia_display_init(void *config, int cfg_size, void **device_handle)
{
    dev_custom_display_lcd_config_t *cfg = (dev_custom_display_lcd_config_t *)config;
    esp_io_expander_handle_t exp = NULL;

    ESP_LOGI(TAG, "Initializing TL021WVC02CT round 480x480 RGB display (%d Hz)",
             cfg->pclk_hz);

    esp_err_t ret = esp_board_device_get_handle("gpio_expander", (void **)&exp);
    if (ret != ESP_OK || exp == NULL) {
        ESP_LOGE(TAG, "gpio_expander handle unavailable: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = qualia_panel_reset_pulse(exp);
    if (ret != ESP_OK) return ret;

    ret = st7701_run_init(exp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ST7701 init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    dev_display_lcd_handles_t *h = calloc(1, sizeof(*h));
    ESP_RETURN_ON_FALSE(h, ESP_ERR_NO_MEM, TAG, "alloc failed");

    esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz           = (uint32_t)cfg->pclk_hz,
            .h_res             = (uint32_t)cfg->h_res,
            .v_res             = (uint32_t)cfg->v_res,
            .hsync_pulse_width = (uint32_t)cfg->hsync_pulse_width,
            .hsync_front_porch = (uint32_t)cfg->hsync_front_porch,
            .hsync_back_porch  = (uint32_t)cfg->hsync_back_porch,
            .vsync_pulse_width = (uint32_t)cfg->vsync_pulse_width,
            .vsync_front_porch = (uint32_t)cfg->vsync_front_porch,
            .vsync_back_porch  = (uint32_t)cfg->vsync_back_porch,
            .flags = {
                .pclk_active_neg = (uint32_t)cfg->pclk_active_neg,
                .hsync_idle_low  = (uint32_t)cfg->hsync_idle_low,
                .vsync_idle_low  = (uint32_t)cfg->vsync_idle_low,
                .de_idle_high    = (uint32_t)cfg->de_idle_high,
                .pclk_idle_high  = (uint32_t)cfg->pclk_idle_high,
            },
        },
        /* Order: B (LSB) → G → R (MSB). The IDF RGB peripheral maps
         * data_gpio_nums[0] to bus bit 0 (LSB). The Qualia panel connector
         * wires DB0=B0..DB5=B5, DB6=G0..DB11=G5, DB12=R0..DB17=R5. */
        .data_gpio_nums = {
            cfg->data_gpio_b0, cfg->data_gpio_b1, cfg->data_gpio_b2,
            cfg->data_gpio_b3, cfg->data_gpio_b4,
            cfg->data_gpio_g0, cfg->data_gpio_g1, cfg->data_gpio_g2,
            cfg->data_gpio_g3, cfg->data_gpio_g4, cfg->data_gpio_g5,
            cfg->data_gpio_r0, cfg->data_gpio_r1, cfg->data_gpio_r2,
            cfg->data_gpio_r3, cfg->data_gpio_r4,
        },
        .data_width    = 16,
        .de_gpio_num    = (int)cfg->de_gpio_num,
        .vsync_gpio_num = (int)cfg->vsync_gpio_num,
        .hsync_gpio_num = (int)cfg->hsync_gpio_num,
        .pclk_gpio_num  = (int)cfg->pclk_gpio_num,
        .disp_gpio_num  = -1,
        .bits_per_pixel = (uint8_t)cfg->bits_per_pixel,
        .flags = {
            .fb_in_psram = true,
        },
    };

    ret = esp_lcd_new_rgb_panel(&panel_cfg, &h->panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_rgb_panel failed: %s", esp_err_to_name(ret));
        free(h);
        return ret;
    }

    ESP_ERROR_CHECK(esp_lcd_panel_reset(h->panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(h->panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(h->panel_handle, true));

    ESP_LOGI(TAG, "TL021WVC02CT display initialized");
    *device_handle = h;
    return ESP_OK;
}

static int qualia_display_deinit(void *device_handle)
{
    dev_display_lcd_handles_t *h = (dev_display_lcd_handles_t *)device_handle;
    if (h) {
        if (h->panel_handle) {
            esp_lcd_panel_disp_on_off(h->panel_handle, false);
            esp_lcd_panel_del(h->panel_handle);
        }
        free(h);
    }
    return ESP_OK;
}

CUSTOM_DEVICE_IMPLEMENT(display_lcd, qualia_display_init, qualia_display_deinit);
