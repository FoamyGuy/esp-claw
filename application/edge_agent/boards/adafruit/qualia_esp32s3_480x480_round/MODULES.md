# Qualia ESP32-S3 RGB-666 + TL021WVC02CT 2.1" Round 480x480 — Module & Pin Reference

Board: `qualia_esp32s3_rgb666_small_round`
Chip: ESP32-S3 (16MB QIO Flash / 8MB OPI PSRAM)
Manufacturer: Adafruit
Carrier board: [Adafruit Qualia ESP32-S3 for TTL RGB-666 Displays](https://www.adafruit.com/product/5800)
Display: [2.1" Round 480x480 ST7701 TFT with capacitive touch (TL021WVC02CT-B1323)](https://www.adafruit.com/product/5792)

Sources:
- Display init code: https://cdn-shop.adafruit.com/product-files/5792/TL021WVC-B1323+SPI+Init+Code.txt
- Display spec sheet: https://cdn-shop.adafruit.com/product-files/5792/Specification_TL021WVC02CT-B1323B.pdf
- ST7701 datasheet: https://cdn-shop.adafruit.com/product-files/5792/ST7701+Datasheet.pdf
- Touch I2C address: Adafruit Learn touch usage page (CST826 at 0x15)

---

## 1. I2C Bus

| Signal | GPIO |
|--------|------|
| SDA    | 8    |
| SCL    | 18   |

Devices on bus: PCA9554A expander (0x3F), CST826 touch (0x15), STEMMA QT.

---

## 2. PCA9554A I/O Expander

I2C address: 0x3F (7-bit)

| Bit | Signal      | Direction |
|-----|-------------|-----------|
| 0   | CLK         | Output    |
| 1   | CS          | Output    |
| 2   | RESET       | Output    |
| 3   | MOSI        | Output    |
| 4   | Backlight   | Input     |
| 5   | UP button   | Input     |
| 6   | DN button   | Input     |
| 7   | (unused)    | Output    |

Driver: `espressif/esp_io_expander_tca9554` (PCA9554A and TCA9554A are register-compatible per `espressif/esp-bsp#335`).

---

## 3. RGB Display — TL021WVC02CT, 2.1" Round 480x480

Driver IC: ST7701 (3-wire SPI for config + RGB-666 dot-clock for pixel data)
Wiring on Qualia: 16 data lines (RGB-565)

### Control lines

| Signal | GPIO |
|--------|------|
| DE     | 17   |
| VSYNC  | 3    |
| HSYNC  | 46   |
| DCLK   | 9    |

### Data lines

Red (R1-R5): 1, 2, 42, 41, 40
Green (G0-G5): 21, 47, 48, 45, 38, 39
Blue (B0-B4): 10, 11, 12, 13, 14

### Timings

| Parameter         | Value          | Spec range |
|-------------------|----------------|------------|
| Pixel clock       | 16,000,000 Hz  | 10-30 MHz, typ 17 |
| Resolution        | 480 × 480      | fixed      |
| HSYNC pulse width | 8              | 2-255 (typ 25) |
| HSYNC back porch  | 20             | 2-255 (typ 30) |
| HSYNC front porch | 8              | 2- (typ 4)  |
| VSYNC pulse width | 8              | 2-254 (typ 8) |
| VSYNC back porch  | 20             | 2-254 (typ 20) |
| VSYNC front porch | 8              | 2- (typ 15) |
| PCLK active neg   | true           |            |

### Init sequence

ST7701 requires multi-byte parameter blocks (some commands take up to 16 data bytes) plus two delays. The init blob in `setup_device.c` is encoded as `{ CMD_OP, len, cmd, params... }` chunks plus `DELAY_OP, ms/10` markers, and walked at boot. The full init is ~27 commands and is bit-banged through the PCA9554A as 9-bit 3-wire SPI.

---

## 4. Capacitive Touch — CST826

| Item              | Value |
|-------------------|-------|
| I2C address       | 0x15  |
| I2C bus           | shared with expander (SDA=8, SCL=18) |
| INT GPIO          | not connected to S3 |
| RESET GPIO        | not connected to S3 |
| Driver            | `espressif/esp_lcd_touch_cst816s` (CST826 is same family) |

CST826 only responds to I2C reads after a touch event. `CONFIG_ESP_LCD_TOUCH_CST816S_DISABLE_READ_ID=y` is set in `sdkconfig.defaults.board` to skip the chip-ID read at init.

---

## 5. GPIO Reserved / Not Available

| GPIO    | Reason |
|---------|--------|
| 19, 20  | Native USB on S3 |
| 26-37   | Reserved for Octal PSRAM |

---

## 6. Flash & PSRAM

| Parameter   | Value         |
|-------------|---------------|
| Flash size  | 16MB          |
| Flash mode  | QIO @ 80MHz   |
| PSRAM size  | 8MB           |
| PSRAM mode  | Octal @ 80MHz |

---

## 7. Console / USB

Native USB CDC. `CONFIG_ESP_CONSOLE_USB_CDC=y`.
