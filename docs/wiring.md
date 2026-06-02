# Wiring

All display devices use a 128×64 SSD1306 OLED over I2C, except the Waveshare ESP32-C6 Touch LCD 1.47, which has an integrated TFT — no external display needed.

The OLED typically has a fixed I2C address of `0x3C`. Power it from the board's 3.3 V rail (some breakouts accept 5 V too — check your module's datasheet).

---

## AI-Thinker ESP32-CAM

The ESP32-CAM's standard I2C pins (GPIO21/22) are used by the camera interface. The OLED is wired to two repurposed camera pins instead.

| OLED pin | ESP32-CAM pin | GPIO | Notes |
|---|---|---|---|
| VCC | 3V3 | — | 3.3 V only |
| GND | GND | — | |
| SDA | HREF | GPIO14 | Camera HREF — repurposed for I2C SDA |
| SCL | PCLK | GPIO15 | Camera PCLK — repurposed for I2C SCL |

`Wire.begin(14, 15)` is called explicitly in firmware to set these non-default pins.

**Locale button:** built-in **BOOT button (GPIO0)** — no external wiring needed. Press to cycle locales.

**Flashing:** The ESP32-CAM has no USB port. You need a USB-to-serial adapter (FTDI / CH340) wired to UART0:

| Adapter | ESP32-CAM |
|---|---|
| TX | GPIO3 (U0RXD) |
| RX | GPIO1 (U0TXD) |
| GND | GND |
| 5V | 5V |

To enter bootloader mode: connect **IO0 → GND**, then press RST (or power-cycle). After uploading, disconnect IO0 from GND and press RST to boot normally.

---

## ESP32 DevKit

| OLED pin | ESP32 pin | GPIO | Notes |
|---|---|---|---|
| VCC | 3V3 | — | 3.3 V only |
| GND | GND | — | |
| SDA | D21 | GPIO21 | Hardware I2C default |
| SCL | D22 | GPIO22 | Hardware I2C default |

`Wire.begin()` is called without arguments — uses the hardware I2C defaults.

**Locale button:** built-in **BOOT button (GPIO0)** — no external wiring needed.

---

## Waveshare ESP32-C6-Zero

The C6-Zero exposes raw ESP32-C6 GPIOs on its 0.1″ headers — silkscreen labels match the GPIO numbers directly. Note: GPIO 22 and 23 (the ESP32-C6 Arduino variant's default I2C pins) are **not** broken out, so the firmware explicitly initialises I2C on GPIO 6/7 instead — the variant's `Wire1` defaults, which Waveshare does break out.

| OLED pin | C6-Zero pin | GPIO | Notes |
|---|---|---|---|
| VCC | 3V3 | — | 3.3 V only |
| GND | GND | — | |
| SDA | 6 | GPIO6 | `Wire1` default SDA in the C6 Arduino variant |
| SCL | 7 | GPIO7 | `Wire1` default SCL in the C6 Arduino variant |

`Wire.begin(6, 7)` is called explicitly in `setup()` for this target.

**Locale button:** built-in **BOOT button (GPIO9)** — no external wiring needed.

**Flashing:** USB-C only, same auto-reset behaviour as the Touch LCD board — enumerates as `/dev/cu.usbmodem2301`. Serial output requires the USB-CDC build flags (already set for this env in `platformio.ini`); without them, `Serial.println()` writes to UART0 (GPIO16/17), which is unconnected on this board.

---

## Waveshare ESP32-C6-Zero + 2.4″ ILI9341 SPI TFT

The `esp32c6_zero_ili9341` target drives a generic red "2.4″ TFT SPI 240×320 V1.3" board (ILI9341 controller) over hardware SPI, rendered with LovyanGFX (TFT_eSPI does not compile for the ESP32-C6).

| Display pin | C6-Zero pin | GPIO | Notes |
|---|---|---|---|
| VCC | 3V3 | — | 3.3 V only |
| GND | GND | — | |
| CS | 5 | GPIO5 | chip select |
| RESET | 3 | GPIO3 | |
| DC | 4 | GPIO4 | data/command |
| SDI (MOSI) | 19 | GPIO19 | |
| SCK | 18 | GPIO18 | |
| LED | 3V3 | — | backlight |
| SDO (MISO) | 20 | GPIO20 | |

The on-board touch (XPT2046) and SD slot are left unconnected.

**Orientation:** build flag `-DTFT_ROTATION` in `platformio.ini` (default `5` = portrait 240×320). This clone needs `rgb_order=true` + `invert=false` for correct colour and an all-rotation boot wipe to clear a GRAM-offset strip its flipped rotations leave behind. Landscape is not supported (the clone misreports per-rotation dimensions).

**Locale button:** built-in **BOOT button (GPIO9)**.

**Flashing:** USB-C; uses `huge_app.csv` partitions (LovyanGFX + WiFi + HTTPClient is large).

---

## Arduino Uno R4 WiFi

| OLED pin | Arduino pin | Notes |
|---|---|---|
| VCC | 5V | Most SSD1306 breakouts accept 3.3–5 V |
| GND | GND | |
| SDA | A4 (SDA) | Hardware I2C |
| SCL | A5 (SCL) | Hardware I2C |

**Locale button:** wire one leg to **D7**, the other leg to **GND**. `INPUT_PULLUP` is configured in firmware — no external resistor needed.

```
D7 ──┤ button ├── GND
```

The Uno R4's I2C is on the dedicated SDA/SCL pins (A4/A5 on the edge connector). Do not use the separate QWIIC/Stemma connector unless you remap the pins.

**WiFi-status LED matrix:** no wiring needed — the firmware uses the board's onboard 12×8 red LED matrix to show a happy face when WiFi is connected and a sad face when it isn't.

---

## Waveshare ESP32-C6 Touch LCD 1.47

**No external wiring needed.** Display, touch controller, and IMU are all integrated on the board, powered and programmed over USB-C.

**Pin assignments** (Touch variant — differs from the non-Touch LCD-1.47):

| Function | GPIO | Notes |
|---|---|---|
| Display SPI SCK | 1 | |
| Display SPI MOSI | 2 | |
| Display CS | 14 | |
| Display DC | 15 | |
| Display RST | 22 | |
| Display BL | 23 | Only effective after the JD9853 register init in `src/lvgl_ui.cpp` |
| Touch I2C SDA | 18 | Shared bus with the IMU |
| Touch I2C SCL | 19 | |
| Touch RST | 20 | |
| Touch INT | 21 | |
| Touch I2C addr | `0x63` | AXS5106L |
| IMU I2C addr | `0x6B` | QMI8658 (on the same bus as touch) |

**Locale input:** built-in **BOOT button (GPIO9)** *or* a tap anywhere on the screen.

**Orientation:** the QMI8658 accelerometer detects landscape vs portrait and triggers a layout rebuild — no hardware setup needed.

**Flashing:** USB-C only. Do not connect an FTDI adapter at the same time; UART0 contention bricks the flash. Auto-reset into bootloader works via `/dev/cu.usbmodem2301`.

---

## OLED module notes

- Almost all cheap SSD1306 128×64 modules have a fixed I2C address of `0x3C`. A few variants support `0x3D` via a solder jumper — if the OLED doesn't initialise, check the jumper.
- Keep I2C wire runs short (under ~30 cm) to avoid signal integrity issues at 400 kHz.
- The SSD1306 needs only VCC, GND, SDA, and SCL — the `RST` pin on some modules can be left unconnected.
