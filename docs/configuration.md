# Configuration and Setup

## Project layout

After cloning, the repo looks like this before building:

```
home-hub-firmware/
├── platformio.ini
├── src/
│   ├── main.cpp                       # one source file, all targets via #ifdef
│   ├── lvgl_ui.cpp                    # C6 Touch LCD LVGL UI (no-op for other targets)
│   ├── lvgl_ui_s3.cpp                 # S3 LCD-2.8 LVGL UI, no touch (no-op for other targets)
│   └── orientation.cpp                # C6 + S3 accelerometer poller, shared (no-op for other targets)
├── scripts/version.py                 # injects git commit hash at build time
├── include/
│   ├── esp32c6_waveshare_lcd/
│   │   ├── lvgl_ui.h                  # public UI API
│   │   ├── orientation.h              # orientation poller API
│   │   ├── lv_conf.h                  # LVGL build config (committed)
│   │   └── arduino_secrets.h          # you create this (gitignored)
│   ├── esp32s3_waveshare_lcd/         # same four files as above, adapted for this board
│   ├── esp32c6_zero/arduino_secrets.h # you create this (gitignored)
│   └── uno_r4_wifi/arduino_secrets.h  # you create this (gitignored)
├── lib/
│   └── esp_lcd_touch_axs5106l/        # vendored supplier touch driver
└── tests/
    ├── display-basic/                 # standalone Arduino_GFX hardware test
    └── lvgl-basic/                    # standalone LVGL test
```

Each `arduino_secrets.h` is gitignored — they are never pushed to GitHub. A `.example` template is committed alongside.

---

## Device credentials (arduino_secrets.h)

Create the secrets file for your board in its include directory:

| Board | Secrets file path |
|---|---|
| Arduino Uno R4 WiFi | `include/uno_r4_wifi/arduino_secrets.h` |
| Waveshare ESP32-C6 Touch LCD | `include/esp32c6_waveshare_lcd/arduino_secrets.h` |
| Waveshare ESP32-S3-LCD-2.8 | `include/esp32s3_waveshare_lcd/arduino_secrets.h` |
| Waveshare ESP32-C6-Zero | `include/esp32c6_zero/arduino_secrets.h` |

All files use the same format — five values:

```cpp
#pragma once

#define SECRET_SSID  "your-wifi-ssid"
#define SECRET_PASS  "your-wifi-password"
#define PROXY_HOST   "netatmo-hub.local"   // or Pi's IP address
#define PROXY_PORT   8080
#define DEVICE_NAME  "my-device"           // shown on the hub status page
```

`PROXY_HOST` can be either the mDNS hostname (`netatmo-hub.local`) or the Pi's IP. If mDNS is unreliable on your network, use the IP and assign a static DHCP lease for the Pi in your router — see the server repo's [raspberry-pi-setup.md](https://github.com/vcchstrandberg/netatmo-home-hub/blob/main/docs/raspberry-pi-setup.md).

`DEVICE_NAME` is sent as the `X-Device-Name` HTTP header on every `/weather` request, but it's only the **initial** friendly name. Once the device first appears in the hub's web UI, rename it there — server-side names persist across reflashes. Identity is keyed on the board's MAC address (`X-Device-Id` header, also sent automatically), so three identical units flashed from the same secrets file still appear as three distinct devices.

---

## Locale and units

Four locales are built into every firmware image and cycled at runtime via the BOOT button (all targets) or screen tap (C6 Touch LCD).

| Index | Locale | Language | Temp | Pressure | Rain |
|---|---|---|---|---|---|
| 0 | `sv-SE` | Svenska | °C | hPa | mm |
| 1 | `en-US` | English (US) | °F | inHg | in |
| 2 | `en-GB` | English (UK) | °C | hPa | mm |
| 3 | `fr-FR` | Français | °C | hPa | mm |

The city name is pulled from the Netatmo API and shown on the outdoor card (OLED targets) or header bar (C6 Touch LCD).

---

## Building and flashing

Install PlatformIO Core if you haven't already:

```bash
pip install platformio
```

From the repo root:

```bash
# Compile only
pio run -e uno_r4_wifi
pio run -e esp32c6_zero
pio run -e esp32c6_waveshare_lcd
pio run -e esp32s3_waveshare_lcd

# Compile and upload
pio run -e uno_r4_wifi            --target upload
pio run -e esp32c6_zero           --target upload
pio run -e esp32c6_waveshare_lcd  --target upload
pio run -e esp32s3_waveshare_lcd  --target upload
```

The first build for each environment downloads the required toolchain and libraries automatically. The two C6 targets share the pioarduino platform (~300 MB, one-time); both LVGL envs (`esp32c6_waveshare_lcd`, `esp32s3_waveshare_lcd`) additionally pull Arduino_GFX, LVGL, and FastIMU.

---

## Waveshare ESP32-C6 Touch LCD flashing

USB-C cable only — **never** connect an FTDI adapter at the same time, the UART0 contention bricks the flash. Auto-reset into bootloader works via `/dev/cu.usbmodem2301`; no manual BOOT+RST dance needed.

```bash
pio run -e esp32c6_waveshare_lcd --target upload
```

---

## Waveshare ESP32-S3-LCD-2.8 flashing

USB-C cable only, same auto-reset behaviour as the C6 Touch LCD. Both boards can enumerate under the same generic Espressif USB-JTAG descriptor, so if more than one is ever connected at once, always pass an explicit `--upload-port` — don't rely on auto-detect (see [Finding the USB port](#finding-the-usb-port) below).

```bash
pio run -e esp32s3_waveshare_lcd --target upload --upload-port /dev/cu.usbmodem2301
```

---

## Waveshare ESP32-C6-Zero flashing

USB-C only, same auto-reset behaviour as the Touch LCD — enumerates as `/dev/cu.usbmodem2301`. The build flags `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1` route `Serial` to the USB-Serial-JTAG bridge; without them the default UART0 pins (GPIO16/17) are unconnected on this board.

```bash
pio run -e esp32c6_zero --target upload
```

---

## Finding the USB port

**macOS / Linux:**
```bash
ls /dev/cu.usbmodem*   # macOS
ls /dev/ttyACM*        # Linux
```
Plug in the board, run the command, then unplug and run again — the entry that disappears is your board.

**Windows:** Device Manager → **Ports (COM & LPT)**.

PlatformIO auto-detects the port when exactly one board is connected. Both C6 boards and the S3 board enumerate under the same generic Espressif USB-JTAG/serial descriptor (`VID:PID=303A:1001`), so if more than one is plugged in at once, auto-detect can't tell them apart — always pass an explicit `--upload-port` in that case.

---

## Serial monitor

Each board prints boot diagnostics and runtime status at 115200 baud:

```bash
pio device monitor -e uno_r4_wifi
pio device monitor -e esp32c6_zero
pio device monitor -e esp32c6_waveshare_lcd
pio device monitor -e esp32s3_waveshare_lcd
```

Press **Ctrl-C** to exit. Typical output:

```
=== Boot ===
Connecting to: YourWiFi
City: Stockholm
In: 21.50  Out: 8.30
Orientation: portrait
Locale: en-US
```

> Note: on the C6 targets (`esp32c6_waveshare_lcd` and `esp32c6_zero`) and the S3 target (`esp32s3_waveshare_lcd`), `Serial` is routed to the USB-Serial-JTAG bridge via the build flags `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`. Without these, the default Serial port (UART0) is unconnected on the Waveshare boards.

If `pio device monitor` fails (e.g. in non-TTY contexts), use Python `pyserial` directly:

```bash
python3 -c "import serial; s=serial.Serial('/dev/cu.usbmodem2301',115200); [print(s.readline().decode(errors='replace').rstrip()) for _ in iter(int,1)]"
```
