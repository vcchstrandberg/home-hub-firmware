# Firmware Architecture

Device-side architecture for the display clients. The server/system-level view (Netatmo → Pi proxy → devices, token refresh, proxy internals) lives in the hub repo: [netatmo-home-hub/docs/architecture.md](https://github.com/vcchstrandberg/netatmo-home-hub/blob/main/docs/architecture.md).

All targets share one `src/main.cpp`, branching by build-flag `#ifdef`. Devices call the Pi's `GET /weather` over plain HTTP and render the flat JSON — no TLS, no OAuth, no tokens on the device.

---

## Boot Sequence

```mermaid
flowchart TD
    A(["Power On / Reset"]) --> B["Init display + Serial\n(Waveshare LVGL boards: LVGL + backlight PWM)"]
    B --> S["Boot splash\napp version · build date · git hash\n5 s"]
    S --> F["Show Connecting…"]
    F --> G["Connect to WiFi"]
    G --> H{Connected?}
    H -->|"No — ESP32: retry every 500 ms up to 60×\nUno R4: retry every 10 s up to 3×"| G
    H -->|Yes| J["fetchWeatherData()"]
    J --> K{"HTTP 200?"}
    K -->|No| ERR["showError(hub_unreachable)"]
    K -->|Yes| L["parseWeather()\nupdate globals\n(Waveshare LVGL boards: apply backlight level)"]
    L --> M["drawCard()"]
    ERR --> M
    M --> N(["Enter Main Loop"])
```

---

## Main Loop

```mermaid
flowchart TD
    start(["Loop iteration"]) --> btn{"BOOT / D7 button\nor screen tap (C6 Touch LCD)?"}

    btn -->|"Yes — debounced 300 ms"| locale["Advance locale\nsv-SE → en-US → en-GB → fr-FR\nshowLocale() 1.5 s → drawCard()"]
    btn -->|No| orient

    locale --> orient{"Waveshare LVGL boards:\norientation changed?"}
    orient -->|"Yes — accelerometer poll ~4 Hz,\n600 ms stable"| rebuild["setOrientation()\nrebuild layout → drawCard()\n(C6: restore active page; S3: no pages)"]
    orient -->|No| card
    rebuild --> card

    card{"CARD_MS elapsed?"}
    card -->|"Yes — OLED boards only\n(both Waveshare LVGL boards show a full dashboard always)"| rotate["Advance card 0→1→2→0\ndrawCard()"]
    card -->|No| fetch
    rotate --> fetch

    fetch{"FETCH_MS elapsed?"}
    fetch -->|No| sleep["delay / pump LVGL"]
    fetch -->|Yes| http["GET http://pi:8080/weather"]

    http --> ok{"HTTP 200?"}
    ok -->|No| err["showError(hub_unreachable)"]
    ok -->|Yes| parse["parseWeather()\nupdate globals → drawCard()\n(Waveshare LVGL boards: apply backlight)"]

    err --> sleep
    parse --> sleep
    sleep --> start
```

**Timing by board:**

| Board | Card rotation (CARD_MS) | Fetch interval (FETCH_MS) |
|---|---|---|
| ESP32-C6-Zero | 5 s | 5 min |
| Uno R4 WiFi | 5 s | 60 s |
| Waveshare ESP32-C6 Touch LCD | Never — full dashboard | 5 min |
| Waveshare ESP32-S3-LCD-2.8 | Never — full dashboard | 5 min |

The Uno R4 fetches more frequently because its WiFi module (ESP32-S3 co-processor) keeps the connection open; the ESP32 boards use the same always-on polling approach at a slower rate to reduce Netatmo API load. Both Waveshare LVGL boards show their full dashboard at once, so neither rotates cards.

---

## Software Stack

```mermaid
flowchart TB
    subgraph fw_stack["Firmware — C++17 (Arduino)"]
        main2["main.cpp\nApplication logic (all targets via #ifdef)"]
        json["ArduinoJson\nJSON parsing"]
        subgraph display_libs["Display"]
            u8g2["U8g2\nSSD1306 OLED\n(C6-Zero · Uno R4)"]
            gfx["Arduino_GFX + LVGL 8.4\nJD9853 TFT (C6 Touch LCD)\nstock ST7789 TFT (S3 LCD-2.8)"]
        end
        subgraph net_libs["Network"]
            httpc["HTTPClient\n(ESP32 / ESP32-C6 / ESP32-S3)"]
            wificlient["WiFiClient raw HTTP\n(Uno R4)"]
        end
        imu["FastIMU\nQMI8658 accelerometer\n(both Waveshare LVGL boards)"]
        main2 --- json
        main2 --- display_libs
        main2 --- net_libs
        main2 --- imu
    end

    subgraph platforms["PlatformIO Platforms"]
        renesas["renesas-ra\n(Uno R4)"]
        pioarduino["pioarduino espressif32\n(C6 Touch LCD · C6-Zero · C6-Zero ILI9341 · S3 LCD-2.8 — arduino-esp32 3.x)"]
    end

    fw_stack --> platforms
```

All four ESP32 targets are pinned to the same pioarduino platform release (an unpinned `platform = espressif32` was found to resolve inconsistently between a machine with an existing package cache and a from-scratch CI runner — see `docs/production-readiness.md`'s CI section); the two LVGL boards (C6 Touch LCD, S3 LCD-2.8) additionally pull in Arduino_GFX, LVGL and FastIMU for their integrated panel and accelerometer — the C6 additionally uses the AXS5106L touch driver, which the S3 board doesn't have.

---

## Hardware Overview

```mermaid
flowchart TB
    subgraph uno["Arduino Uno R4 WiFi"]
        ra4m1["Renesas RA4M1\nMain MCU — sketch runs here\n48 MHz Cortex-M4"]
        esp32s3["ESP32-S3 co-processor\nWiFi 802.11 b/g/n\nInternal UART (AT modem protocol)"]
        ra4m1 <-->|"WiFiS3 library"| esp32s3
    end

    subgraph zero["Waveshare ESP32-C6-Zero"]
        riscv0["ESP32-C6 — RISC-V, 160 MHz\nWiFi 6 · BOOT on GPIO9"]
        note0["SSD1306 on GPIO6/7\n(variant defaults 22/23 not broken out)"]
    end

    subgraph c6["Waveshare ESP32-C6 Touch LCD 1.47"]
        riscv["ESP32-C6 — RISC-V, 160 MHz\n802.11 b/g/n/ax (WiFi 6)"]
        tft["JD9853 TFT, 172×320 px\n(landscape 320×172) · SPI\nBacklight PWM on GPIO23 (time-of-day dimming)"]
        touch["AXS5106L touch + QMI8658 IMU\nshared I2C bus"]
        riscv --- tft
        riscv --- touch
    end

    subgraph s3["Waveshare ESP32-S3-LCD-2.8 (non-touch)"]
        xtensas3["ESP32-S3 — Xtensa LX7, 240 MHz\n802.11 b/g/n · BOOT on GPIO0"]
        tft3["Stock ST7789 TFT, 240×320 px\n(landscape 320×240) · SPI\nBacklight PWM on GPIO5 (time-of-day dimming)"]
        imu3["QMI8658 IMU only — no touch controller\nmounted rotated 90° vs. the C6 board"]
        xtensas3 --- tft3
        xtensas3 --- imu3
    end

    oled["SSD1306 OLED\n128×64 px · I2C"]

    uno -->|"I2C A4/A5"| oled
    zero -->|"I2C GPIO6/7"| oled
```

The Waveshare ESP32-C6 Touch LCD uses a JD9853 panel (not stock ST7789) that needs a custom register init before its backlight regulator turns on — see [wiring.md](wiring.md) and the display driver in `src/lvgl_ui.cpp`. The ESP32-S3-LCD-2.8 uses a stock ST7789 instead, so Arduino_GFX's built-in init is enough — see `src/lvgl_ui_s3.cpp`. Both boards' backlights run on LEDC PWM so the hub can drive time-of-day dimming.
