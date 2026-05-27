# Qualia ESP32-S3 RGB-666 — Module & Pin Reference

Board: `qualia_esp32s3_rgb666`  
Chip: ESP32-S3 (16MB QIO Flash / 8MB OPI PSRAM)  
Manufacturer: Adafruit  
Product: [Adafruit Qualia ESP32-S3 for TTL RGB-666 Displays](https://www.adafruit.com/product/5800)  
Schematic: https://learn.adafruit.com/adafruit-qualia-esp32-s3-for-rgb666-displays/downloads

Sources used for GPIO verification:
- Adafruit Learn Guide — "CircuitPython Display Setup" (TFT_PINS dict, board.c)
- Adafruit forum post: https://forums.adafruit.com/viewtopic.php?t=213663 (I2C pins confirmed SCL=IO18, SDA=IO8)
- CircuitPython GitHub issues #10712 and #10613 (TL040HDS20 timings, two independent real-hardware reports)
- Adafruit Learn Guide — Pinouts page (PCA9554A address 0x3F)

---

## 1. I2C Bus

| Signal | GPIO | Notes |
|--------|------|-------|
| SDA    | 8    | Shared: PCA9554A expander, touch controller, STEMMA QT |
| SCL    | 18   | Shared: same bus |

---

## 2. PCA9554A I/O Expander

I2C address: **0x3F** (default; solder jumpers on board reverse can change it)

| Expander bit | Signal   | Direction | Notes |
|-------------|----------|-----------|-------|
| 0           | CLK      | Output    | 3-wire SPI clock to display panel |
| 1           | CS       | Output    | 3-wire SPI chip select |
| 2           | RESET    | Output    | Display panel reset |
| 3           | MOSI     | Output    | 3-wire SPI data out |
| 4           | Backlight| Input     | TPS61169 backlight control |
| 5           | UP btn   | Input     | Right-angle button, active low |
| 6           | DN btn   | Input     | Right-angle button, active low |
| 7           | —        | Output    | (tied high at idle) |

Direction register (reg 3): `0x78` = `0b01111000` (bits 3–6 inputs, bits 0,1,2,7 outputs)  
Invert register (reg 2): `0x00` (no inversion)

---

## 3. RGB Dot-Clock Display — TL040HDS20, 4" 720×720

### Control lines

| Signal | GPIO | Notes |
|--------|------|-------|
| DE     | 17   | Data enable |
| VSYNC  | 3    | Vertical sync |
| HSYNC  | 46   | Horizontal sync |
| DCLK   | 9    | Pixel clock |

### Data lines — Red (R1–R5, 5 pins; R0 shorted to R1 on PCB)

| Signal | GPIO |
|--------|------|
| R1     | 1    |
| R2     | 2    |
| R3     | 42   |
| R4     | 41   |
| R5     | 40   |

### Data lines — Green (G0–G5, 6 pins)

| Signal | GPIO |
|--------|------|
| G0     | 21   |
| G1     | 47   |
| G2     | 48   |
| G3     | 45   |
| G4     | 38   |
| G5     | 39   |

### Data lines — Blue (B0–B4, 5 pins)

| Signal | GPIO |
|--------|------|
| B0     | 10   |
| B1     | 11   |
| B2     | 12   |
| B3     | 13   |
| B4     | 14   |

Total data lines: 16 (RGB-565; board routes 5+6+5 despite "RGB-666" product name)

### Timing — TL040HDS20 (720×720 square panel)

| Parameter          | Value      | Notes |
|--------------------|------------|-------|
| Pixel clock        | 16,000,000 Hz | Source: CircuitPython issues #10712, #10613 |
| H resolution       | 720        |       |
| V resolution       | 720        |       |
| HSYNC pulse width  | 2          |       |
| HSYNC front porch  | 46         |       |
| HSYNC back porch   | 44         |       |
| VSYNC pulse width  | 2          |       |
| VSYNC front porch  | 16         |       |
| VSYNC back porch   | 18         |       |
| HSYNC idle low     | false      |       |
| VSYNC idle low     | false      |       |
| DE idle high       | false      |       |
| PCLK active neg    | true       | pclk_active_high=False in CircuitPython terms |
| PCLK idle high     | false      |       |
| Init sequence      | none       | TL040HDS20 requires no SPI init bytes |

> ⚠️ **Verify before merge:** timing values sourced from CircuitPython real-hardware reports.
> Recommend cross-checking against TL040HDS20 datasheet, particularly pixel clock and porch values.

---

## 4. Touch Controller (TL040HDS20**CT** capacitive-touch variant only)

| Parameter   | Value  | Notes |
|-------------|--------|-------|
| Chip        | FT6206 (FocalTouch) | |
| I2C address | 0x48   | On TL040HDS20CT; may differ on other panels |
| I2C bus     | i2c_master (SDA=8, SCL=18) | |

Touch controller is on the same shared I2C bus as the PCA9554A expander.  
No-touch variant (TL040HDS20, no CT suffix) has no touch controller.

---

## 5. GPIO Reserved / Not Available

| GPIO     | Reason unavailable |
|----------|--------------------|
| 19, 20   | Native USB (USB-C wired directly to ESP32-S3) |
| 26–37    | Reserved — Octal PSRAM (8MB OPI) |

---

## 6. Flash & PSRAM

| Parameter   | Value        |
|-------------|--------------|
| Flash size  | 16MB         |
| Flash mode  | QIO          |
| Flash speed | 80MHz        |
| PSRAM size  | 8MB          |
| PSRAM mode  | Octal (OPI)  |
| PSRAM speed | 80MHz        |

---

## 7. Console / USB

| Parameter | Value |
|-----------|-------|
| Interface | Native USB CDC |
| Notes     | No UART bridge chip; USB-C connects directly to ESP32-S3 native USB port |

`CONFIG_ESP_CONSOLE_USB_CDC=y` required in sdkconfig.defaults.board.
