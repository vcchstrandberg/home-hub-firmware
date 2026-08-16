# home-hub-firmware

Firmware for embedded weather display clients of the home hub server. Companion to [`netatmo-home-hub`](https://github.com/vcchstrandberg/netatmo-home-hub), which runs on a Raspberry Pi, handles Netatmo OAuth, and serves the current weather as plain HTTP on the local network.

Devices call `GET http://<pi>:8080/weather` and render the response. No tokens, no TLS, no Netatmo registration on the device side.

## Supported targets

| Environment | Board | MCU | Display | Inputs |
|---|---|---|---|---|
| `esp32c6_waveshare_lcd` | Waveshare ESP32-C6 Touch LCD 1.47 | ESP32-C6 RISC-V, 160 MHz | Integrated 172×320 IPS TFT (JD9853) | Capacitive touch (AXS5106L), accelerometer (QMI8658), BOOT button |
| `esp32s3_waveshare_lcd` | Waveshare ESP32-S3-LCD-2.8 (non-touch) | ESP32-S3, 240 MHz | Integrated 240×320 IPS TFT (stock ST7789) | Accelerometer (QMI8658), BOOT button, external KY-040 rotary encoder (page nav + locale button) — no touch panel |
| `esp32c6_zero` | Waveshare ESP32-C6-Zero | ESP32-C6 RISC-V, 160 MHz | SSD1306 128×64 OLED (GPIO6/7) | BOOT button (GPIO9) |
| `esp32c6_zero_ili9341` | Waveshare ESP32-C6-Zero | ESP32-C6 RISC-V, 160 MHz | 2.4″ ILI9341 240×320 SPI TFT (SCLK18/MOSI19/MISO20/DC4/CS5/RST3) | BOOT button (GPIO9) |
| `uno_r4_wifi` | Arduino Uno R4 WiFi | Renesas RA4M1, 48 MHz | SSD1306 128×64 OLED (A4/A5) | D7 button, onboard 12×8 LED matrix (WiFi status) |

OLED targets use U8g2 with three rotating cards. Both Waveshare LVGL boards use **Arduino_GFX + LVGL 8.4** with a card-based dashboard that auto-switches between landscape and portrait via the on-board accelerometer: `esp32c6_waveshare_lcd` cycles dashboard/forecast/about pages via touch swipe, while `esp32s3_waveshare_lcd` has no touch panel — it instead cycles Inside/Outside/Weather/About pages via an external KY-040 rotary encoder (rotate = page, push = locale). The `esp32c6_zero_ili9341` target drives an external 2.4″ ILI9341 SPI TFT with **LovyanGFX** (TFT_eSPI doesn't build for the ESP32-C6) showing a full stacked portrait dashboard.

## Features

- **Central OAuth hub** — the Pi holds the single Netatmo refresh token; devices carry no credentials
- **Plain HTTP to the hub** — no TLS on devices, no per-device app registration
- **Multi-locale with unit conversion** — Svenska, English US, English UK, Français; °C↔°F, hPa↔inHg, mm↔in
- **Runtime locale switching** — BOOT button on every board, or **tap the screen** on the C6 Touch LCD
- **Automatic orientation** (both Waveshare LVGL boards) — accelerometer detects how the device is held and rebuilds the dashboard between landscape and portrait (320×172 / 172×320 on the C6, 320×240 / 240×320 on the S3)
- **LVGL UI** (both Waveshare LVGL boards) — anti-aliased Montserrat fonts, themed colors, modal overlays for boot/connecting/locale/error
- **Time-of-day dimming** (both Waveshare LVGL boards) — the hub computes a backlight level from the station's sunrise/sunset and sends it in `/weather`; the display fades between day and night brightness automatically
- **High-pressure sun** (all three TFT targets) — a golden sun appears beside the outdoor reading when the barometric pressure is high (≥ 1020 hPa)
- **WiFi-status smiley** (Uno R4 WiFi only) — the onboard 12×8 LED matrix shows a happy face when WiFi is connected, a sad face when it isn't; updates automatically as the link drops or recovers
- **Device naming** — set `DEVICE_NAME` in `arduino_secrets.h`; sent as `X-Device-Name` HTTP header so the hub labels devices without server config
- **Error hold** — display stays on the error screen until the hub reconnects; stale data never re-shown after a lost connection

## Quick start

Set up `arduino_secrets.h` for your board (see [docs/configuration.md](docs/configuration.md)), then:

```sh
pio run -e esp32c6_waveshare_lcd --target upload
```

## Layout

```
home-hub-firmware/
├── platformio.ini
├── src/
│   ├── main.cpp                       # one source file, all targets via #ifdef
│   ├── lvgl_ui.cpp                    # C6 Touch LCD LVGL widgets + display driver glue
│   ├── lvgl_ui_s3.cpp                 # S3 LCD-2.8 LVGL widgets + display driver glue (no touch)
│   ├── tft_ui.cpp                     # C6-Zero ILI9341 LovyanGFX dashboard
│   └── orientation.cpp                # C6 + S3 accelerometer poller (shared, per-board axis mapping)
├── scripts/version.py                 # injects git commit hash at build time
├── include/
│   ├── esp32c6_waveshare_lcd/         # lvgl_ui.h, orientation.h, lv_conf.h, arduino_secrets.h
│   ├── esp32s3_waveshare_lcd/         # lvgl_ui.h, orientation.h, lv_conf.h, arduino_secrets.h
│   ├── esp32c6_zero/                  # arduino_secrets.h
│   ├── esp32c6_zero_ili9341/          # tft_ui.h, arduino_secrets.h
│   └── uno_r4_wifi/                   # arduino_secrets.h
├── lib/
│   └── esp_lcd_touch_axs5106l/        # vendored supplier touch driver
├── tests/
│   ├── display-basic/                 # standalone Arduino_GFX hardware verification sketch
│   └── lvgl-basic/                    # standalone LVGL test sketch
└── docs/
    ├── architecture.md                # device boot/loop, software stack, hardware
    ├── configuration.md               # arduino_secrets, build, flash, serial
    ├── display-layout.md              # OLED card and C6 LVGL dashboard layouts
    ├── wiring.md                      # pin connections per board
    ├── production-readiness.md        # WiFi provisioning + OTA paths
    └── revision-history.md            # firmware version log
```

`arduino_secrets.h` is gitignored — `.example` templates committed alongside.

## Tests

Each subdirectory under `tests/` is an independent PlatformIO project for hardware verification without the full firmware:

- `tests/display-basic/` — minimal supplier helloworld; confirms pins, JD9853 init, and backlight.
- `tests/lvgl-basic/` — minimal LVGL stack on the same hardware.

Build either with `pio run` from inside its own directory.

## Documentation

- [Architecture](docs/architecture.md) — device boot sequence, main loop, software stack, hardware overview
- [Configuration](docs/configuration.md) — board secrets, building, flashing, serial
- [Display layout](docs/display-layout.md) — OLED card designs and the C6 LVGL dashboard (landscape + portrait)
- [Wiring](docs/wiring.md) — pin connections per board
- [Production readiness](docs/production-readiness.md) — WiFi provisioning + OTA paths for going beyond a single home
- [Revision history](docs/revision-history.md) — firmware version log

## Companion server

The Pi-side proxy lives in [vcchstrandberg/netatmo-home-hub](https://github.com/vcchstrandberg/netatmo-home-hub). See that repo for OAuth setup, the web status UI, the `/weather` JSON format, and Pi install instructions.
