// Color-TFT UI implementation for ESP32-C6-Zero + 2.4" ILI9341 SPI display.
// Compiled only when ESP32C6_ZERO_TFT is defined; a no-op otherwise.

#ifdef ESP32C6_ZERO_TFT

#include "tft_ui.h"
#include <Arduino.h>
#include <LovyanGFX.hpp>

// ─── Panel wiring (ESP32-C6-Zero, broken-out GPIOs) ───────────────────────────
//   VCC→3V3  GND→GND  LED→3V3   (display is 3.3V only)
//   SCLK=18  MOSI=19  MISO=20  DC=4  CS=5  RST=3
// This is a generic red "2.4\" TFT SPI 240x320 V1.3" ILI9341 clone. Its quirks,
// found empirically on the bench, are baked into the config below:
//   - rgb_order=true, invert=false  → correct colours (red=red, etc.)
//   - rotation 6                    → upright, non-mirrored portrait
//   - all-rotation boot wipe        → the flipped rotation leaves a stale GRAM
//                                     strip that a single fillScreen misses
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI       _bus;

public:
  LGFX() {
    auto cfg = _bus.config();
    cfg.spi_host   = SPI2_HOST;
    cfg.spi_mode   = 0;
    cfg.freq_write = 27000000;
    cfg.freq_read  =  8000000;
    cfg.pin_sclk   = 18;
    cfg.pin_mosi   = 19;
    cfg.pin_miso   = 20;
    cfg.pin_dc     =  4;
    _bus.config(cfg);
    _panel.setBus(&_bus);

    auto pcfg = _panel.config();
    pcfg.pin_cs    =  5;
    pcfg.pin_rst   =  3;
    pcfg.pin_busy  = -1;
    pcfg.invert    = false;
    pcfg.rgb_order = true;
    _panel.config(pcfg);

    setPanel(&_panel);
  }
};

static LGFX tft;

// Orientation is selectable at build time via -DTFT_ROTATION in the env's
// build_flags. On this clone the upright, non-mirrored options are the flipped
// rotations: 5 = portrait (240x320, default), 6 = landscape (320x240). 7 / 4
// are their 180-degree siblings. The layout below adapts to whatever
// width()/height() the chosen rotation reports, so no other change is needed.
#ifndef TFT_ROTATION
#  define TFT_ROTATION 5  // portrait
#endif

static int gW = 240; // actual width  after setRotation (filled in init)
static int gH = 320; // actual height after setRotation (filled in init)

// ─── Small drawing helpers ────────────────────────────────────────────────────
static void labelAt(int x, int y, const char* text) {
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(x, y);
  tft.print(text);
}

static void valueAt(int x, int y, int size, const char* text) {
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(size);
  tft.setCursor(x, y);
  tft.print(text);
}

// Left-margin convenience wrappers (x = 6).
static void label(int y, const char* text)            { labelAt(6, y, text); }
static void value(int y, int size, const char* text)  { valueAt(6, y, size, text); }

static void centered(int y, int size, uint16_t color, const char* text) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(size);
  int w = tft.textWidth(text);
  tft.setCursor((gW - w) / 2, y);
  tft.print(text);
}

// Small sun used as the high-pressure indicator: a filled golden disc ringed
// by eight bold rays. Mirrors the LVGL sun on the Waveshare target, drawn here
// with LovyanGFX. Rays use drawWideLine (anti-aliased, thick) so they read as
// rays rather than stray pixels; the disc is filled last to cover their inner
// ends cleanly. (cx,cy) is the disc center, r its radius.
static void drawSun(int cx, int cy, int r) {
  const uint16_t col = tft.color565(0xFF, 0xC1, 0x07);  // amber gold
  const float w = 2.5f;   // ray thickness
  const int in  = r + 3;  // ray inner start (just outside the disc)
  const int out = r + 8;  // ray outer tip
  const int di  = (in  * 7) / 10;  // diagonal component ~ 1/sqrt(2)
  const int doo = (out * 7) / 10;
  tft.drawWideLine(cx, cy - in, cx, cy - out, w, col);   // N
  tft.drawWideLine(cx, cy + in, cx, cy + out, w, col);   // S
  tft.drawWideLine(cx - in, cy, cx - out, cy, w, col);   // W
  tft.drawWideLine(cx + in, cy, cx + out, cy, w, col);   // E
  tft.drawWideLine(cx + di, cy - di, cx + doo, cy - doo, w, col);  // NE
  tft.drawWideLine(cx - di, cy - di, cx - doo, cy - doo, w, col);  // NW
  tft.drawWideLine(cx + di, cy + di, cx + doo, cy + doo, w, col);  // SE
  tft.drawWideLine(cx - di, cy + di, cx - doo, cy + doo, w, col);  // SW
  tft.fillCircle(cx, cy, r, col);
}

// ─── Forecast weather icon ────────────────────────────────────────────────────
// A small icon set drawn with LovyanGFX primitives, mirroring the Waveshare
// LVGL icons and the OLED glyph mapping. Categories collapse the fuller met.no
// symbol set the same way the other displays do.
enum { WI_SUN, WI_PARTLY, WI_CLOUD, WI_RAIN, WI_SNOW, WI_THUNDER, WI_FOG, WI_UNKNOWN };

static int fcCategory(const char* code) {
  if (!code || !*code) return WI_UNKNOWN;
  String s(code);
  int u = s.lastIndexOf('_');            // strip _day/_night/_polartwilight
  if (u > 0) {
    String suf = s.substring(u + 1);
    if (suf == "day" || suf == "night" || suf == "polartwilight") s = s.substring(0, u);
  }
  if (s.indexOf("thunder") >= 0) return WI_THUNDER;
  if (s.indexOf("sleet")   >= 0) return WI_RAIN;   // sleet rendered as rain
  if (s.indexOf("snow")    >= 0) return WI_SNOW;
  if (s.indexOf("rain")    >= 0) return WI_RAIN;
  if (s == "fog")                return WI_FOG;
  if (s == "clearsky" || s == "fair") return WI_SUN;
  if (s == "partlycloudy")       return WI_PARTLY;
  if (s == "cloudy")             return WI_CLOUD;
  return WI_UNKNOWN;
}

// A grey cloud centered near (cx,cy): two puffs on a rounded body. Kept
// compact so it fits beside a single text line without touching neighbours.
static void drawCloud(int cx, int cy, uint16_t col) {
  tft.fillCircle(cx - 6, cy + 2, 6, col);
  tft.fillCircle(cx + 5, cy,     9, col);
  tft.fillRoundRect(cx - 12, cy + 2, 24, 8, 4, col);
}

// Draw a weather icon centered at (cx,cy), spanning roughly ±16 px. Compact by
// design: it sits to the right of the forecast temps on one text line.
static void drawWeatherIcon(int cx, int cy, int cat) {
  const uint16_t grey  = tft.color565(0xB0, 0xBE, 0xC5);
  const uint16_t blue  = tft.color565(0x4F, 0xC3, 0xF7);
  const uint16_t amber = tft.color565(0xFF, 0xD5, 0x4F);
  const uint16_t mute  = tft.color565(0x90, 0xA4, 0xAE);
  switch (cat) {
    case WI_SUN:
      drawSun(cx, cy, 7);
      break;
    case WI_PARTLY:
      drawSun(cx - 6, cy - 5, 5);
      drawCloud(cx + 3, cy + 3, grey);
      break;
    case WI_CLOUD:
      drawCloud(cx, cy, grey);
      break;
    case WI_RAIN:
      drawCloud(cx, cy - 3, grey);
      for (int i = 0; i < 3; i++)
        tft.drawWideLine(cx - 8 + i * 8, cy + 9, cx - 10 + i * 8, cy + 16, 2, blue);
      break;
    case WI_SNOW:
      drawCloud(cx, cy - 3, grey);
      for (int i = 0; i < 3; i++) tft.fillCircle(cx - 8 + i * 8, cy + 12, 2, TFT_WHITE);
      break;
    case WI_THUNDER:
      drawCloud(cx, cy - 3, grey);
      tft.drawWideLine(cx + 2, cy + 8,  cx - 3, cy + 14, 2, amber);
      tft.drawWideLine(cx - 3, cy + 14, cx + 2, cy + 14, 2, amber);
      tft.drawWideLine(cx + 2, cy + 14, cx - 3, cy + 20, 2, amber);
      break;
    case WI_FOG:
      for (int i = 0; i < 4; i++)
        tft.drawWideLine(cx - 11, cy - 5 + i * 6, cx + 11, cy - 5 + i * 6, 2, grey);
      break;
    default:
      drawCloud(cx, cy, mute);
      break;
  }
}

namespace TftUI {

void init() {
  tft.init();
  // The clone's flipped rotation has a quirky GRAM offset, so a single
  // fillScreen can leave stale pixels in an unaddressed strip. Wipe across
  // every rotation once at boot to guarantee the whole panel is black.
  for (int r = 0; r < 8; r++) {
    tft.setRotation(r);
    tft.fillScreen(TFT_BLACK);
  }
  tft.setRotation(TFT_ROTATION);
  gW = tft.width();
  gH = tft.height();
  tft.fillScreen(TFT_BLACK);
}

void showBootSplash(const char* version, const char* date, const char* commit) {
  tft.fillScreen(TFT_BLACK);
  centered(70,  2, TFT_YELLOW, "Netatmo");
  centered(98,  2, TFT_YELLOW, "Home Hub");
  char buf[24];
  snprintf(buf, sizeof(buf), "v%s", version);
  centered(150, 2, TFT_WHITE, buf);
  centered(190, 1, TFT_CYAN, date);
  centered(206, 1, TFT_CYAN, commit);
}

void showConnecting(const char* hint, const char* ssid) {
  tft.fillScreen(TFT_BLACK);
  centered(130, 2, TFT_YELLOW, hint);
  centered(160, 2, TFT_WHITE, ssid);
}

void showLocale(const char* name, const char* code) {
  tft.fillScreen(TFT_BLACK);
  centered(120, 2, TFT_CYAN, "Language");
  centered(150, 3, TFT_WHITE, name);
  centered(185, 2, TFT_CYAN, code);
}

void showError(const char* title, const char* detail, const char* retrying) {
  tft.fillScreen(TFT_BLACK);
  centered(90,  3, TFT_RED, "ERROR");
  centered(140, 2, TFT_WHITE, title);
  if (detail) centered(170, 2, TFT_WHITE, detail);
  centered(210, 1, TFT_CYAN, retrying);
}

void drawDashboard(const char* indoorLabel, const char* humidityLabel,
                   float indoorTemp, int indoorHumidity, const char* tempUnit,
                   const char* outdoorLabel, const char* pressureLabel,
                   const char* pressureUnit, float outdoorTemp, float pressure,
                   uint8_t pressureDecimals, bool highPressure,
                   const char* rainLabel, const char* rainUnit,
                   uint8_t rainDecimals, float rain1h, float rain24h,
                   bool isRaining,
                   bool fcHasData, const char* fcTitle, const char* fcSymbol,
                   float fcTempMax, float fcTempMin, float fcPrecip,
                   const char* fcNa) {
  tft.fillScreen(TFT_BLACK);
  char buf[32];

  // No title bar: the section labels (indoor/outdoor/rain) come from the
  // active locale, so a hardcoded English title would be out of place.
  //
  // Single stacked layout. Auto-switching to a wide layout by aspect ratio
  // proved unreliable on this clone (it misreports width/height for the
  // flipped rotations it needs), so this sticks to the one layout that works.
  // Four equal sections now: indoor / outdoor / rain / tomorrow's forecast.
  const int sec = gH / 4;
  // Divider sits a few px above the section boundary, in the gap below the
  // sub-label — never through text. Sub-labels are pulled up accordingly so the
  // tighter 4-section layout stays clear.
  const int divY = sec - 4;

  // Indoor (section 0)
  int b = 0;
  label(b + 4, indoorLabel);
  snprintf(buf, sizeof(buf), "%.1f %s", indoorTemp, tempUnit);
  value(b + 16, 4, buf);
  snprintf(buf, sizeof(buf), "%s%d%%", humidityLabel, indoorHumidity);
  label(b + 52, buf);
  tft.drawFastHLine(0, b + divY, gW, TFT_DARKGREY);

  // Outdoor (section 1)
  b = sec;
  label(b + 4, outdoorLabel);
  snprintf(buf, sizeof(buf), "%.1f %s", outdoorTemp, tempUnit);
  value(b + 16, 4, buf);
  // High-pressure sun, in the open space to the right of the temperature/unit
  // (same 1020 hPa cutoff as the Waveshare target). Anchored to the measured
  // temperature width so it always clears the "C"/"F" glyph.
  if (highPressure) {
    tft.setTextSize(4);
    int sx = 6 + tft.textWidth(buf) + 24;
    drawSun(sx, b + 30, 8);
  }
  snprintf(buf, sizeof(buf), "%s%.*f %s", pressureLabel,
           (int)pressureDecimals, pressure, pressureUnit);
  label(b + 52, buf);
  tft.drawFastHLine(0, b + divY, gW, TFT_DARKGREY);

  // Rain (section 2)
  b = 2 * sec;
  label(b + 4, rainLabel);
  if (isRaining) {
    tft.fillCircle(gW - 16, b + 8, 5, TFT_CYAN);
  }
  snprintf(buf, sizeof(buf), "1h:  %.*f %s", (int)rainDecimals, rain1h, rainUnit);
  value(b + 16, 2, buf);
  snprintf(buf, sizeof(buf), "24h: %.*f %s", (int)rainDecimals, rain24h, rainUnit);
  value(b + 38, 2, buf);
  tft.drawFastHLine(0, b + divY, gW, TFT_DARKGREY);

  // Forecast — tomorrow (section 3, last). Hi/Lo on the left, a compact weather
  // icon centered on the right of that same line, precipitation below. Shows a
  // placeholder when the hub has no forecast yet. No trailing divider.
  b = 3 * sec;
  label(b + 4, fcTitle);
  if (fcHasData) {
    snprintf(buf, sizeof(buf), "%.0f / %.0f %s", fcTempMax, fcTempMin, tempUnit);
    value(b + 16, 3, buf);
    // Place the icon by MEASURING the rendered temp width (this clone's font
    // metrics/rotation are quirky, so a hard-coded x can't be trusted). If the
    // temps leave room to their right, sit the icon there on the same line;
    // otherwise tuck it into the bottom-right, clear of both text lines.
    tft.setTextSize(3);
    int tw  = tft.textWidth(buf);
    int cat = fcCategory(fcSymbol);
    if (6 + tw + 38 <= gW) drawWeatherIcon(6 + tw + 22, b + 24, cat);
    else                   drawWeatherIcon(gW - 20, b + 58, cat);
    snprintf(buf, sizeof(buf), "%s %.*f %s", rainLabel, (int)rainDecimals, fcPrecip, rainUnit);
    label(b + 46, buf);
  } else {
    value(b + 22, 2, fcNa);
  }
}

} // namespace TftUI

#endif // ESP32C6_ZERO_TFT
