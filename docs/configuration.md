# Configuration and Setup

## Project layout

After cloning, the repo looks like this before building:

```
home-hub-firmware/
├── platformio.ini
├── src/
│   ├── main.cpp                       # one source file, all targets via #ifdef
│   ├── lvgl_ui.cpp                    # C6 LVGL UI (no-op for other targets)
│   └── orientation.cpp                # C6 accelerometer poller
├── scripts/version.py                 # injects git commit hash at build time
├── include/
│   ├── esp32c6_waveshare_lcd/
│   │   ├── lvgl_ui.h                  # public UI API
│   │   ├── orientation.h              # orientation poller API
│   │   ├── lv_conf.h                  # LVGL build config (committed)
│   │   └── arduino_secrets.h          # you create this (gitignored)
│   ├── esp32cam/arduino_secrets.h     # you create this (gitignored)
│   ├── esp32dev/arduino_secrets.h     # you create this (gitignored)
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
| AI-Thinker ESP32-CAM | `include/esp32cam/arduino_secrets.h` |
| ESP32 DevKit | `include/esp32dev/arduino_secrets.h` |
| Arduino Uno R4 WiFi | `include/uno_r4_wifi/arduino_secrets.h` |
| Waveshare ESP32-C6 Touch LCD | `include/esp32c6_waveshare_lcd/arduino_secrets.h` |

All four files use the same format — five values:

```cpp
#pragma once

#define SECRET_SSID  "your-wifi-ssid"
#define SECRET_PASS  "your-wifi-password"
#define PROXY_HOST   "netatmo-hub.local"   // or Pi's IP address
#define PROXY_PORT   8080
#define DEVICE_NAME  "my-device"           // shown on the hub status page
```

`PROXY_HOST` can be either the mDNS hostname (`netatmo-hub.local`) or the Pi's IP. If mDNS is unreliable on your network, use the IP and assign a static DHCP lease for the Pi in your router — see the server repo's [raspberry-pi-setup.md](https://github.com/vcchstrandberg/netatmo-home-hub/blob/main/docs/raspberry-pi-setup.md).

`DEVICE_NAME` is sent as the `X-Device-Name` HTTP header on every `/weather` request. The hub uses it to label each device on its status page. Reflash with a new name and the hub picks it up on the next poll — no server config needed.

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
pio run -e esp32cam
pio run -e esp32dev
pio run -e uno_r4_wifi
pio run -e esp32c6_waveshare_lcd

# Compile and upload
pio run -e esp32cam               --target upload
pio run -e esp32dev               --target upload
pio run -e uno_r4_wifi            --target upload
pio run -e esp32c6_waveshare_lcd  --target upload
```

The first build for each environment downloads the required toolchain and libraries automatically. The C6 build fetches the pioarduino platform (~300 MB, one-time) plus Arduino_GFX, LVGL, and FastIMU.

---

## ESP32-CAM flashing

The ESP32-CAM has no USB port — it requires a USB-to-serial adapter (FTDI or CH340) wired to its UART0 pins (GPIO1/GPIO3). See [wiring.md](wiring.md) for the connection diagram.

To enter bootloader mode before uploading:
1. Connect **IO0 → GND**.
2. Press **RST** (or briefly disconnect and reconnect power).
3. Run `pio run -e esp32cam --target upload`.
4. After upload completes, **disconnect IO0 from GND** and press RST to boot normally.

---

## Waveshare ESP32-C6 Touch LCD flashing

USB-C cable only — **never** connect an FTDI adapter at the same time, the UART0 contention bricks the flash. Auto-reset into bootloader works via `/dev/cu.usbmodem2301`; no manual BOOT+RST dance needed.

```bash
pio run -e esp32c6_waveshare_lcd --target upload
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

PlatformIO auto-detects the port when exactly one board is connected.

---

## Serial monitor

Each board prints boot diagnostics and runtime status at 115200 baud:

```bash
pio device monitor -e esp32cam
pio device monitor -e esp32dev
pio device monitor -e uno_r4_wifi
pio device monitor -e esp32c6_waveshare_lcd
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

> Note: on the C6 Touch LCD target, `Serial` is routed to the USB-Serial-JTAG bridge via the build flags `-DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`. Without these, the default Serial port (UART0) is unconnected on the Waveshare board.

If `pio device monitor` fails (e.g. in non-TTY contexts), use Python `pyserial` directly:

```bash
python3 -c "import serial; s=serial.Serial('/dev/cu.usbmodem2301',115200); [print(s.readline().decode(errors='replace').rstrip()) for _ in iter(int,1)]"
```
