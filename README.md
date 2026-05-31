# home-hub-firmware

Firmware for embedded weather display clients of the home hub server. Companion to [`netatmo-home-hub`](https://github.com/vcchstrandberg/netatmo-home-hub), which runs on a Raspberry Pi, handles Netatmo OAuth, and serves the current weather as plain HTTP on the local network.

Devices call `GET http://<pi>:8080/weather` and render the response. No tokens, no TLS, no Netatmo registration on the device side.

## Supported targets

| Environment | Board | MCU | Display | Inputs |
|---|---|---|---|---|
| `esp32c6_waveshare_lcd` | Waveshare ESP32-C6 Touch LCD 1.47 | ESP32-C6 RISC-V, 160 MHz | Integrated 172×320 IPS TFT (JD9853) | Capacitive touch (AXS5106L), accelerometer (QMI8658), BOOT button |
| `esp32c6_zero` | Waveshare ESP32-C6-Zero | ESP32-C6 RISC-V, 160 MHz | SSD1306 128×64 OLED (GPIO6/7) | BOOT button (GPIO9) |
| `esp32c6_zero_ili9341` | Waveshare ESP32-C6-Zero | ESP32-C6 RISC-V, 160 MHz | 2.4″ ILI9341 240×320 SPI TFT (SCLK18/MOSI19/MISO20/DC4/CS5/RST3) | BOOT button (GPIO9) |
| `esp32cam` | AI-Thinker ESP32-CAM | Xtensa LX6, 240 MHz | SSD1306 128×64 OLED (GPIO14/15) | BOOT button |
| `esp32dev` | Generic ESP32 DevKit | Xtensa LX6, 240 MHz | SSD1306 128×64 OLED (GPIO21/22) | BOOT button |
| `uno_r4_wifi` | Arduino Uno R4 WiFi | Renesas RA4M1, 48 MHz | SSD1306 128×64 OLED (A4/A5) | D7 button |

OLED targets use U8g2 with three rotating cards. The ESP32-C6 target uses **Arduino_GFX + LVGL 8.4** with a card-based dashboard that automatically switches between landscape and portrait layouts based on the on-board accelerometer.

## Features

- **Central OAuth hub** — the Pi holds the single Netatmo refresh token; devices carry no credentials
- **Plain HTTP to the hub** — no TLS on devices, no per-device app registration
- **Multi-locale with unit conversion** — Svenska, English US, English UK, Français; °C↔°F, hPa↔inHg, mm↔in
- **Runtime locale switching** — BOOT button on every board, or **tap the screen** on the C6 Touch LCD
- **Automatic orientation** (C6 only) — accelerometer detects how the device is held and rebuilds the dashboard between landscape (320×172, side-by-side cards) and portrait (172×320, stacked cards)
- **LVGL UI** (C6 only) — anti-aliased Montserrat fonts, themed colors, modal overlays for boot/connecting/locale/error
- **Time-of-day dimming** (C6 Touch LCD only) — the hub computes a backlight level from the station's sunrise/sunset and sends it in `/weather`; the display fades between day and night brightness automatically
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
│   ├── lvgl_ui.cpp                    # C6 LVGL widgets + display driver glue
│   ├── tft_ui.cpp                     # C6-Zero ILI9341 LovyanGFX dashboard
│   └── orientation.cpp                # C6 accelerometer poller
├── scripts/version.py                 # injects git commit hash at build time
├── include/
│   ├── esp32c6_waveshare_lcd/         # lvgl_ui.h, orientation.h, lv_conf.h, arduino_secrets.h
│   ├── esp32c6_zero/                  # arduino_secrets.h
│   ├── esp32c6_zero_ili9341/          # tft_ui.h, arduino_secrets.h
│   ├── esp32cam/                      # arduino_secrets.h
│   ├── esp32dev/                      # arduino_secrets.h
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
