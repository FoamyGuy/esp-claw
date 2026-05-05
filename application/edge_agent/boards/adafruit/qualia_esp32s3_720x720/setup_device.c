/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileCopyrightText: 2026 Anne Barela for Adafruit Industries
 * SPDX-License-Identifier: Apache-2.0
 *
 * Board bring-up for Adafruit Qualia ESP32-S3 RGB-666 with TL040HDS20 4" 720x720 display.
 *
 * This board uses an RGB dot-clock interface, which has no display_lcd:rgb sub_type in
 * esp_board_manager v0.5.3. Both devices use type:custom.
 *
 * Display device: Pattern C from §8.7 of espclaw_new_board.md.
 * Returns dev_display_lcd_handles_t* so display_hal.c, the display arbiter, and
 * lua_module_display can consume it via esp_board_device_get_handle("display_lcd", ...).
 *
 * Expander device: PCA9554A is initialized before display_lcd in board_devices.yaml,
 * so the expander handle is available when display_lcd init runs if needed.
 *
 * GPIO source: Adafruit CircuitPython board.c / Learn guide "CircuitPython Display Setup"
 * Timing source: CircuitPython GitHub issues #10712 and #10613 (two independent sources)
 * Expander source: Adafruit Learn guide TFT_IO_EXPANDER table and i2c_init_sequence
 */

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"
#include "driver/i2c_master.h"
#include "esp_io_expander.h"
#include "esp_io_expander_tca9554.h"

/* MANDATORY umbrella headers — do not include sub-headers directly */
#include "esp_board_manager_includes.h"
#include "gen_board_device_custom.h"

/* Pattern C displays return dev_display_lcd_handles_t. The umbrella header only
 * exposes it when CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT is enabled, which it
 * isn't here (we register the panel as type:custom). Mirror the struct locally —
 * display_hal.c consumes it by layout. Keep field order in sync with
 * managed_components/espressif__esp_board_manager/devices/dev_display_lcd/dev_display_lcd.h. */
typedef struct {
    esp_lcd_panel_io_handle_t  io_handle;
    esp_lcd_panel_handle_t     panel_handle;
} dev_display_lcd_handles_t;

static const char *TAG = "QUALIA_ESP32S3_RGB666";

/* ---------------------------------------------------------------------------
 * PCA9554A I/O expander factory entry
 *
 * dev_gpio_expander.c calls io_expander_factory_entry_t() after probing the bus.
 * PCA9554A is register-compatible with TCA9554A, so we use the espressif
 * tca9554 driver. (See esp32_s3_korvo2_v3/setup_device.c for the canonical pattern.)
 *
 * At runtime the expander is accessible via:
 *   esp_io_expander_handle_t exp = NULL;
 *   esp_board_device_get_handle("gpio_expander", (void **)&exp);
 * ------------------------------------------------------------------------- */
esp_err_t io_expander_factory_entry_t(i2c_master_bus_handle_t i2c_handle,
                                      const uint16_t dev_addr,
                                      esp_io_expander_handle_t *handle_ret)
{
    esp_err_t ret = esp_io_expander_new_i2c_tca9554(i2c_handle, dev_addr, handle_ret);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_io_expander_new_i2c_tca9554 failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/* ---------------------------------------------------------------------------
 * TL040HDS20 RGB dot-clock display — Pattern C
 *
 * Pixel clock: 16 MHz
 * Resolution:  720 × 720
 * Interface:   RGB-565 (16 data lines; board PCB README: "5-6-5 RGB color")
 * Init seq:    None (bytes() empty) — TL040HDS20 needs no SPI init sequence.
 *              Source: CircuitPython issues #10712 and #10613.
 *
 * REVIEWER NOTE: pclk_active_neg=true maps to CircuitPython pclk_active_high=False.
 * Verify this polarity is correct against TL040HDS20 datasheet before merging.
 * If display is all-black or shows a single color but panel LED backlight is on,
 * flip this flag first.
 * ------------------------------------------------------------------------- */

static int qualia_display_init(void *config, int cfg_size, void **device_handle)
{
    dev_custom_display_lcd_config_t *cfg =
        (dev_custom_display_lcd_config_t *)config;

    ESP_LOGI(TAG, "Initializing TL040HDS20 RGB dot-clock display (%d Hz, %dx%d)",
             cfg->pclk_hz, cfg->h_res, cfg->v_res);

    dev_display_lcd_handles_t *h = calloc(1, sizeof(*h));
    ESP_RETURN_ON_FALSE(h, ESP_ERR_NO_MEM, TAG, "Failed to allocate display handle");

    esp_lcd_rgb_panel_config_t panel_cfg = {
        .clk_src         = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz             = (uint32_t)cfg->pclk_hz,
            .h_res               = (uint32_t)cfg->h_res,
            .v_res               = (uint32_t)cfg->v_res,
            .hsync_pulse_width   = (uint32_t)cfg->hsync_pulse_width,
            .hsync_front_porch   = (uint32_t)cfg->hsync_front_porch,
            .hsync_back_porch    = (uint32_t)cfg->hsync_back_porch,
            .vsync_pulse_width   = (uint32_t)cfg->vsync_pulse_width,
            .vsync_front_porch   = (uint32_t)cfg->vsync_front_porch,
            .vsync_back_porch    = (uint32_t)cfg->vsync_back_porch,
            .flags = {
                /* pclk_active_high=False in CircuitPython → pclk_active_neg=true here */
                .pclk_active_neg = (uint32_t)cfg->pclk_active_neg,
                .hsync_idle_low  = (uint32_t)cfg->hsync_idle_low,
                .vsync_idle_low  = (uint32_t)cfg->vsync_idle_low,
                .de_idle_high    = (uint32_t)cfg->de_idle_high,
                .pclk_idle_high  = (uint32_t)cfg->pclk_idle_high,
            },
        },
        /* RGB data lines — 16 pins total (5+6+5 = RGB-565).
         * GPIO assignments from CircuitPython board.c red/green/blue_pins[]:
         *   R[0..4] = {1, 2, 42, 41, 40}
         *   G[0..5] = {21, 47, 48, 45, 38, 39}
         *   B[0..4] = {10, 11, 12, 13, 14}
         * DE=17, VSYNC=3, HSYNC=46, DCLK=9
         * Source: Adafruit Learn "CircuitPython Display Setup", TFT_PINS dict. */
        .data_gpio_nums = {
            /* R0-R4 */
            cfg->data_gpio_r0, cfg->data_gpio_r1, cfg->data_gpio_r2,
            cfg->data_gpio_r3, cfg->data_gpio_r4,
            /* G0-G5 */
            cfg->data_gpio_g0, cfg->data_gpio_g1, cfg->data_gpio_g2,
            cfg->data_gpio_g3, cfg->data_gpio_g4, cfg->data_gpio_g5,
            /* B0-B4 */
            cfg->data_gpio_b0, cfg->data_gpio_b1, cfg->data_gpio_b2,
            cfg->data_gpio_b3, cfg->data_gpio_b4,
        },
        .data_width      = 16,
        .de_gpio_num     = (int)cfg->de_gpio_num,
        .vsync_gpio_num  = (int)cfg->vsync_gpio_num,
        .hsync_gpio_num  = (int)cfg->hsync_gpio_num,
        .pclk_gpio_num   = (int)cfg->pclk_gpio_num,
        .disp_gpio_num   = -1,       /* no dedicated display-enable pin */
        .bits_per_pixel  = (uint8_t)cfg->bits_per_pixel,
        .flags = {
            .fb_in_psram    = true,  /* 720*720*2 = ~1MB, must be in PSRAM */
            .double_fb      = false, /* single framebuffer to start */
            .no_fb          = false,
        },
        .bounce_buffer_size_px = 0,
    };

    esp_err_t ret = esp_lcd_new_rgb_panel(&panel_cfg, &h->panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_rgb_panel failed: %s", esp_err_to_name(ret));
        free(h);
        return ret;
    }

    /* h->io_handle stays NULL — RGB panels have no separate IO controller */

    ESP_ERROR_CHECK(esp_lcd_panel_reset(h->panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(h->panel_handle));
    /* RGB panel has no disp_gpio (-1); driver returns ESP_ERR_NOT_SUPPORTED.
    Skip — display is enabled by RESET line via PCA9554. */

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

/* YAML device name is "display_lcd" — must match exactly */
CUSTOM_DEVICE_IMPLEMENT(display_lcd, qualia_display_init, qualia_display_deinit);
