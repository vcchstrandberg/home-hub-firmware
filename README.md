# home-hub-firmware

Firmware for embedded weather display clients of the home hub server.
Companion project to [`netatmo-home-hub`](https://github.com/vcchstrandberg/netatmo-home-hub),
which runs on a Raspberry Pi, handles Netatmo OAuth, and exposes the current
weather as plain HTTP on the local network.

Devices call `GET http://<pi>:8080/weather` and render the response. No
tokens, no TLS, no Netatmo registration on the device side.

## Supported targets

| Environment | Board | MCU | Display |
|---|---|---|---|
| `esp32c6_waveshare_lcd` | Waveshare ESP32-C6 Touch LCD 1.47 | ESP32-C6 RISC-V, 160 MHz | Integrated 172×320 IPS TFT (JD9853) |
| `esp32cam` | AI-Thinker ESP32-CAM | Xtensa LX6, 240 MHz | SSD1306 128×64 OLED (GPIO14/15) |
| `esp32dev` | Generic ESP32 DevKit | Xtensa LX6, 240 MHz | SSD1306 128×64 OLED (GPIO21/22) |
| `uno_r4_wifi` | Arduino Uno R4 WiFi | Renesas RA4M1, 48 MHz | SSD1306 128×64 OLED (A4/A5) |

The OLED targets use U8g2. The ESP32-C6 target uses Arduino_GFX with a
custom JD9853 register init.

## Build and flash

Set up `arduino_secrets.h` in the target's include directory with your
Wi-Fi credentials and the hub URL. Then:

```sh
pio run -e esp32c6_waveshare_lcd --target upload
```

## Layout

```
home-hub-firmware/
├── platformio.ini
├── src/main.cpp                       # one source file, all targets via #ifdef
├── scripts/version.py                 # injects git commit hash at build time
├── include/
│   ├── esp32c6_waveshare_lcd/         # LGFX_config.h (Arduino_GFX shim) + arduino_secrets.h
│   ├── esp32cam/                      # arduino_secrets.h
│   ├── esp32dev/                      # arduino_secrets.h
│   └── uno_r4_wifi/                   # arduino_secrets.h
└── tests/
    ├── display-basic/                 # standalone Arduino_GFX hardware verification sketch
    └── lvgl-basic/                    # standalone LVGL test sketch
```

`arduino_secrets.h` is gitignored — see the companion server repo for the
expected format.

## Tests

The two subdirectories under `tests/` are independent PlatformIO projects
useful for hardware verification without the full firmware:

- `tests/display-basic/` — minimal supplier helloworld; confirms pins,
  JD9853 init, and backlight work.
- `tests/lvgl-basic/` — minimal LVGL stack on the same hardware.

Build either with `pio run` from inside its own directory.
