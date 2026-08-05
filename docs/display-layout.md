# Display Layout

All boards show the same weather data. Labels and units follow the active locale.

| Field | Source field | Unit |
|---|---|---|
| Indoor temperature | `indoor_temp` | °C / °F |
| Indoor humidity | `indoor_humidity` | % |
| Air pressure | `pressure` | hPa / inHg |
| Outdoor temperature | `outdoor_temp` | °C / °F |
| Rain last hour | `rain_1h` | mm / in |
| Rain last 24 h | `rain_24h` | mm / in |
| Raining now | `is_raining` | indicator only |
| City name | `city` | string |

---

## SSD1306 OLED — 128×64 px (ESP32-CAM · ESP32 DevKit · Uno R4 WiFi)

Three full-screen cards rotate every 5 seconds. Each card has a 16×16 Open Iconic weather icon, a large primary value in logisoso28 font, and a smaller secondary line.

**Boot splash** (5 s):
```
┌──────────────────────────────┐
│ Netatmo Home Hub             │  ncenB08 font
│ v1.0                         │
│ May 14 2026                  │
│ 0362bcf                      │  ← git commit hash
└──────────────────────────────┘
```

**Locale switch** (1.5 s, on button press):
```
┌──────────────────────────────┐
│ Language:                    │  ncenB08
│                              │
│  Svenska                     │  logisoso16 font
│  sv-SE                       │  ncenB08
└──────────────────────────────┘
```

**Card 0 — Indoor** (sun icon, glyph 69):
```
┌──────────────────────────────┐
│ ☀  INNE / INDOOR             │  icon 16×16 + locale label
│                              │
│  21.5C                       │  logisoso28 — temp + unit per locale
│                              │
│  Fukt: 45%                   │  humidity label + value per locale
└──────────────────────────────┘
```

**Card 1 — Outdoor** (cloud icon, glyph 64):
```
┌──────────────────────────────┐
│ ⛅  Stockholm                 │  icon + city name from API
│                              │
│  8.3C                        │  outdoor temp
│                              │
│  Tryck: 1013hPa              │  pressure label + value per locale
└──────────────────────────────┘
```

**Card 2 — Rain** (rain icon, glyph 67):
```
┌──────────────────────────────┐
│ 🌧  REGN / RAIN           💧 │  💧 shown only when is_raining = true
│                              │
│  1h:  0.6mm                  │  logisoso16
│                              │
│  24h: 3.2mm                  │
└──────────────────────────────┘
```

**Error screen** (on connection failure):
```
┌──────────────────────────────┐
│ ⚙  ERROR                     │  embedded icon glyph 71
│  Hub unreachable             │  g_loc->hub_unreachable
│  (HTTP code)                 │  optional detail
│  Forsoker... / Retrying...   │  g_loc->retrying
└──────────────────────────────┘
```

---

## TFT — 2.4″ ILI9341 240×320 (ESP32-C6-Zero)

`esp32c6_zero_ili9341`, rendered with LovyanGFX. Single full-screen portrait dashboard — no card cycling — split into three equal-height stacked sections sized from the live panel height:

```
┌──────────────────────────────┐
│ INNE                         │  cyan section label
│ 23.4 C                       │  size-4 white value
│ Fukt: 41%                    │
│ ──────────────────────────── │
│ OUTDOOR / city               │
│ 8.2 C                        │
│ Tryck: 1013 hPa              │
│ ──────────────────────────── │
│ REGN                      ●  │  ● = raining dot
│ 1h:  2.4 mm                  │
│ 24h: 8.1 mm                  │
└──────────────────────────────┘
```

**Source:** `src/tft_ui.cpp` (`TftUI` namespace), `drawDashboard()`. Orientation is fixed at build time via `-DTFT_ROTATION` (default `5` = portrait); see [wiring.md](wiring.md) for the clone-specific colour/offset quirks.

**Uno R4 WiFi LED matrix:** separate from the OLED, the onboard 12×8 matrix shows a happy/sad face for WiFi state (`FACE_HAPPY` / `FACE_SAD` in `src/main.cpp`). Authored bottom-up because row 0 maps to the bottom of the matrix.

---

## TFT — Waveshare ESP32-C6 Touch LCD 1.47

172×320 IPS panel driven by Arduino_GFX + LVGL 8.4. The dashboard rebuilds itself when the accelerometer detects a change between landscape (320×172) and portrait (172×320). Single full-screen view in either orientation — no card cycling.

**Source:** widget tree lives in `src/lvgl_ui.cpp`, with `buildLandscape()` and `buildPortrait()` sharing a `createTempCard()` helper, a `createHeader()` helper, and a `createModal()` helper for the overlay screens (boot splash, connecting, locale hint, error).

**Modal overlays** (single multi-line label centered on the full-screen modal container; same code path in both orientations):

| Trigger | Text content |
|---|---|
| Boot splash (5 s) | `Netatmo Home Hub` / `v1.x` / `May 24 2026` / git commit |
| Connecting | `Ansluter WiFi:` / `<SSID>` |
| Locale switch (1.5 s, BOOT button or screen tap) | `Language` / `Svenska` / `sv-SE` |
| Error | `ERROR` / title / detail / `Forsoker...` (on dark red bg) |

**Landscape dashboard (320×172):**

```
 x=0                            x=162                       x=320
 ┌─────────────────────────────────────────────────────────────┐  y=0
 │ Vastra Lassby                                       sv-SE   │  header (teal #024D5C, 24 tall)
 ├─────────────────────────────┬───────────────────────────────┤  y=28
 │ ┌─────────────────────────┐ │ ┌───────────────────────────┐ │
 │ │ INNE (amber #FFA726)    │ │ │ UTE (blue #4FC3F7)        │ │
 │ │                         │ │ │                           │ │
 │ │ 24.7 C                  │ │ │ 18.5 C                    │ │  card 156×102
 │ │                         │ │ │                           │ │
 │ │ Fukt: 41%               │ │ │ Tryck: 1028hPa            │ │
 │ └─────────────────────────┘ │ └───────────────────────────┘ │
 ├─────────────────────────────┴───────────────────────────────┤  y=134
 │ REGN  1h: 0.0mm                              24h: 0.0mm     │  rain row (blue #0277BD, 38 tall)
 └─────────────────────────────────────────────────────────────┘  y=172
```

**Portrait dashboard (172×320):**

```
 x=0                  x=172
 ┌───────────────────────┐  y=0
 │ Vastra Lassby  sv-SE  │  header (24 tall)
 ├───────────────────────┤  y=28
 │ ┌───────────────────┐ │
 │ │ INNE (amber)      │ │
 │ │                   │ │
 │ │ 24.7 C            │ │  indoor card 168×102
 │ │                   │ │
 │ │ Fukt: 41%         │ │
 │ └───────────────────┘ │
 ├───────────────────────┤  y=134
 │ ┌───────────────────┐ │
 │ │ UTE (blue)        │ │
 │ │                   │ │
 │ │ 18.5 C            │ │  outdoor card 168×102
 │ │                   │ │
 │ │ Tryck: 1028hPa    │ │
 │ └───────────────────┘ │
 ├───────────────────────┤  y=240
 │ REGN         1h: 0.0mm│  rain row (68 tall, two lines)
 │              24h: 0.0mm│
 └───────────────────────┘  y=308
```

**Palette** (see `src/lvgl_ui.cpp` for constants):

| Token | Hex | Used by |
|---|---|---|
| `COL_BG` | `#0F1419` | Screen background |
| `COL_HEADER_BG` | `#024D5C` | Header bar |
| `COL_CARD_BG` | `#1A2331` | Inside indoor / outdoor cards |
| `COL_INDOOR_ACC` | `#FFA726` | Indoor card border + "INNE" label |
| `COL_OUTDOOR_ACC` | `#4FC3F7` | Outdoor card border + "UTE" label |
| `COL_RAIN_BG` | `#0277BD` | Rain row bar |
| `COL_MUTED` | `#90A4AE` | Temperature unit |
| `COL_DETAIL` | `#CFD8DC` | Humidity / pressure / locale code |
| `COL_ERROR_BG` | `#611A15` | Error modal bg |

**Fonts:** LVGL's bundled Montserrat at sizes 12, 14, 24, 36. Sizes 24 and 36 must remain enabled in `include/esp32c6_waveshare_lcd/lv_conf.h`. The default font omits Latin-1 supplement glyphs (ÅÄÖ) — those characters are stripped to ASCII (`Vastra Lassby`) by `stripAccents()` in `lvgl_ui.cpp` to avoid placeholder boxes. Switching to a custom Latin-1 Montserrat would let real Swedish characters render natively.

**Touch input:** AXS5106L controller on I2C (SDA=18, SCL=19, addr 0x63). Any tap anywhere on screen cycles the locale, same as the BOOT button.

**Orientation:** QMI8658 accelerometer on the same I2C bus (addr 0x6B). Polled every 250 ms by `Orientation::poll()`; emits a transition when |X| or |Y| dominance has been stable for ≥600 ms. Suppressed while |Z| > 0.85 g (lying flat). On transition, `LvglUI::setOrientation()` rotates the Arduino_GFX panel, updates LVGL's display driver dimensions, cleans the screen, and rebuilds the appropriate layout. main.cpp then calls `drawCard()` so the new layout immediately gets populated.

---

## TFT — Waveshare ESP32-S3-LCD-2.8 (non-touch)

240×320 stock ST7789 panel driven by Arduino_GFX + LVGL 8.4 — see `src/lvgl_ui_s3.cpp`. This board has **no touch panel**, and testing showed no other input on it makes a page-swipe substitute practical, so unlike the C6 Touch LCD's 3-page carousel (dashboard / forecast / about), this board has **no paging at all**: indoor, outdoor, rain, and tomorrow's forecast are all laid out at once in a single 2×2 grid that rebuilds (same grid shape, recomputed cell sizes) when the accelerometer detects landscape vs portrait. There is no About page and no header bar — just the four cards, edge to edge with a 2 px margin/gap.

**Landscape (320×240):**

```
 x=0                            x=157                       x=320
 ┌─────────────────────────────┬───────────────────────────────┐  y=2
 │ INNE (amber #FFA726)        │ UTE (blue #4FC3F7)            │
 │                             │                               │
 │ 24.7 C                      │ 18.5 C                        │  cell ~157×138
 │                             │                               │
 │ Fukt: 41%                   │ Tryck: 1028hPa            ☀   │
 ├─────────────────────────────┼───────────────────────────────┤  y=142
 │ REGN                        │ TOMORROW                      │
 │ 1h: 0.0mm                   │  (icon)  9 / 3 C              │  cell ~157×82
 │ 24h: 0.0mm                  │  REGN: 1.2mm                  │
 └─────────────────────────────┴───────────────────────────────┘  y=224
```

**Portrait (240×320):** same 2×2 composition, cells recomputed as ~118×157 each, indoor/outdoor on top and rain/forecast on the bottom — no separate portrait-specific drawing code, `buildDashboard()` in `src/lvgl_ui_s3.cpp` computes cell geometry from `s_gfx->width()/height()` at build time rather than hardcoding two layouts.

**Palette:** identical tokens to the C6 Touch LCD board above (`COL_BG`, `COL_CARD_BG`, `COL_INDOOR_ACC`, `COL_OUTDOOR_ACC`, `COL_RAIN_BG`, `COL_MUTED`, `COL_DETAIL`, `COL_ERROR_BG`, plus `COL_SUN`/`COL_CLOUD` for the sun icon and forecast weather icons) — see `src/lvgl_ui_s3.cpp` for the hex values.

**Modal overlays:** same boot splash / connecting / locale / error overlays as the C6 board, full-screen, no page-indicator dots (there being nothing to indicate — this board has one screen).

**Orientation:** QMI8658 on I2C SDA=11/SCL=10 (addr 0x6B, same chip and address as the C6 board). This board's IMU is mounted rotated 90° relative to the C6's, so `rotationFromAccel()` in `src/orientation.cpp` swaps the X/Y axis roles for this target — confirmed against real hardware.
