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

static const char *TAG = "QUALIA_ESP32S3_RGB666";

/* PCA9554A pin assignments per Adafruit pins_arduino.h */
#define PCA_BIT_CLK        0    /* TFT_SCK         (output) */
#define PCA_BIT_CS         1    /* TFT_CS          (output) */
#define PCA_BIT_RESET      2    /* TFT_RESET       (output) */
#define PCA_BIT_TOUCH_IRQ  3    /* CPT_IRQ         (input)  */
#define PCA_BIT_BACKLIGHT  4    /* TFT_BACKLIGHT   (output, HIGH = on) */
#define PCA_BIT_BTN_UP     5    /* BUTTON_UP       (input)  */
#define PCA_BIT_BTN_DN     6    /* BUTTON_DOWN     (input)  */
#define PCA_BIT_MOSI       7    /* TFT_MOSI        (output) */


/* Factory function called automatically by the gpio_expander framework code
 * after the base PCA9554A driver has been instantiated.
 *
 * For Qualia: drive output register to a safe idle state (CS high, CLK/MOSI low,
 * RESET low to start) so the panel is held in reset until display init runs.
 * The actual reset-release pulse and any panel-specific SPI init bytes are
 * performed in the display init function below — that's where panel-specific
 * timing needs to live, not here. */
esp_err_t io_expander_factory_entry_t(i2c_master_bus_handle_t i2c_handle,
                                      const uint16_t dev_addr,
                                      esp_io_expander_handle_t *handle_ret)
{
    /* Use TCA9554 driver — register-compatible with PCA9554A.
     * See espressif/esp-bsp#335. */
    esp_err_t ret = esp_io_expander_new_i2c_tca9554(i2c_handle, dev_addr, handle_ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PCA9554A IO expander: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Set initial output levels. The framework already configured directions
     * from output_io_mask in the YAML. Drive backlight HIGH explicitly here so
     * the TPS61169 backlight driver is enabled before any pixel data starts. */
    esp_io_expander_set_level(*handle_ret, BIT(PCA_BIT_CS),        1);
    esp_io_expander_set_level(*handle_ret, BIT(PCA_BIT_RESET),     0);
    esp_io_expander_set_level(*handle_ret, BIT(PCA_BIT_CLK),       0);
    esp_io_expander_set_level(*handle_ret, BIT(PCA_BIT_MOSI),      0);
    esp_io_expander_set_level(*handle_ret, BIT(PCA_BIT_BACKLIGHT), 1);

    return ESP_OK;
}

/* Pulse reset on the panel via expander bit 2.
 * Active low. Hold for 10 ms, release, wait 120 ms for panel to come up. */
static esp_err_t qualia_panel_reset_pulse(esp_io_expander_handle_t exp)
{
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

    ESP_LOGI(TAG, "Initializing TL040HDS20 RGB display (%d Hz, %dx%d)",
             cfg->pclk_hz, cfg->h_res, cfg->v_res);

    /* Pulse RESET on the expander before the panel sees pixel clock.
     * gpio_expander is listed before display_lcd in board_devices.yaml so
     * its handle is available here. */
    esp_err_t ret = esp_board_device_get_handle("gpio_expander", (void **)&exp);
    if (ret != ESP_OK || exp == NULL) {
        ESP_LOGE(TAG, "gpio_expander handle unavailable: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = qualia_panel_reset_pulse(exp);
    if (ret != ESP_OK) {
        return ret;
    }

    /* TL040HDS20 needs no SPI init bytes — empty init sequence per
     * CircuitPython issues #10712 and #10613. Other Qualia panels
     * (HD40015C40, etc.) would bit-bang their init bytes here via the
     * expander before this point. */

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
        .data_width = 16,
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


    ESP_LOGI(TAG, "TL040HDS20 display initialized");
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
