# Firmware Revision History

Server revision history lives in the [server repo](https://github.com/vcchstrandberg/netatmo-home-hub/blob/main/docs/revision-history.md).

| Version | Date | Notes |
|---|---|---|
| v1.6 | 2026-05-25 | **MAC-based identity** — every `/weather` request now carries an `X-Device-Id` header containing the board's MAC address (`WiFi.macAddress()`). Identical units flashed from the same `arduino_secrets.h` are now distinguishable to the hub; the per-device `DEVICE_NAME` becomes an optional initial label that can be renamed in the hub's web UI without reflashing. Paired with server v1.11. |
| v1.5 | 2026-05-24 | **C6 Touch LCD: orientation switching** — QMI8658 accelerometer polled every 250 ms; layout rebuilds between landscape (320×172, two cards side by side) and portrait (172×320, two cards stacked with two-line rain row). Suppressed while lying flat (\|Z\| > 0.85 g) and debounced over a 600 ms stability window. FastIMU library vendored from GitHub tag 1.2.8 since not in the PlatformIO registry. |
| v1.4 | 2026-05-24 | **C6 Touch LCD: touch input** — AXS5106L touch driver vendored under `lib/esp_lcd_touch_axs5106l/`. Any tap anywhere on screen cycles the locale (same as BOOT button). Tap detection via press→release edge in the LVGL indev callback; user callback deferred to next `tick()` with re-entry guard to avoid recursing through the synchronous `showLocale` busy wait. |
| v1.3 | 2026-05-24 | **C6 Touch LCD: LVGL UI** — full migration from TFT_eSPI-style direct drawing to LVGL 8.4 widget tree. Card-based dashboard with amber/blue accent borders, anti-aliased Montserrat fonts (12/14/24/36), themed dark background, modal overlays for boot/connecting/locale/error. Partition table switched to `huge_app.csv` (3 MB app slot). Previous Arduino_GFX-only path replaced; LGFX_config.h shim removed. |
| v1.3-pre | 2026-05-24 | **C6 Touch LCD: display working** — discovered the Touch variant uses completely different pins than the non-Touch LCD-1.47 (BL=23, SCK=1, MOSI=2, RST=22), and that the JD9853 panel needs a custom register init sequence before the backlight will turn on. Migrated from LovyanGFX to Arduino_GFX (the JD9853 init is impractical to express in a LovyanGFX Panel subclass). USB-CDC build flags added so Serial routes to the USB-JTAG bridge. *Internal only, not separately tagged.* |
| v1.2 | 2026-05-15 | **Device naming** — `DEVICE_NAME` added to `arduino_secrets.h`. Sent as `X-Device-Name` HTTP header on every `/weather` request so the hub can display human-readable device names without any server-side config. |
| v1.1 | 2026-05-15 | **Error hold** — display stays on the error screen until the hub reconnects. Previously `g_hasData` stayed `true` on fetch failure, causing stale data to reappear immediately after the error flash. All failure paths now set `g_hasData = false`; `parseWeather()` is the only place that sets it back to `true`. Card timer reset on reconnect. |
| v1.0 | 2026-05-14 | **Initial release** — always-on polling firmware for all four boards. Plain HTTP to the Pi proxy (no TLS, no tokens on device). C6 full dashboard (thermometer graphics, rain dots, indoor/outdoor panels). 3-card OLED cycling for ESP32-CAM, ESP32 DevKit, Uno R4 WiFi. Multi-locale with runtime switching. |

---

## Repo split

This firmware was split out of the [netatmo-home-hub](https://github.com/vcchstrandberg/netatmo-home-hub) monorepo on 2026-05-24 (initial commit on `main` of this repo). Earlier commit history for the firmware files lives in the original repo before that date.
