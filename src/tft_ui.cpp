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

static const int W = 240; // panel width  at rotation 6
static const int H = 320; // panel height at rotation 6

// ─── Small drawing helpers ────────────────────────────────────────────────────
static void label(int y, const char* text) {
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextSize(1);
  tft.setCursor(6, y);
  tft.print(text);
}

static void value(int y, int size, const char* text) {
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(size);
  tft.setCursor(6, y);
  tft.print(text);
}

static void centered(int y, int size, uint16_t color, const char* text) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setTextSize(size);
  int w = tft.textWidth(text);
  tft.setCursor((W - w) / 2, y);
  tft.print(text);
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
  tft.setRotation(6);
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
                   uint8_t pressureDecimals,
                   const char* rainLabel, const char* rainUnit,
                   uint8_t rainDecimals, float rain1h, float rain24h,
                   bool isRaining) {
  tft.fillScreen(TFT_BLACK);
  char buf[32];

  // No title bar: the section labels (indoor/outdoor/rain) come from the
  // active locale, so a hardcoded English title would be out of place. The
  // layout is kept within the rotation-6 panel's safe vertical budget
  // (~y<=230) — this clone wraps content drawn lower back to the top.

  // Indoor
  label(10, indoorLabel);
  snprintf(buf, sizeof(buf), "%.1f %s", indoorTemp, tempUnit);
  value(24, 4, buf);
  snprintf(buf, sizeof(buf), "%s%d%%", humidityLabel, indoorHumidity);
  label(64, buf);
  tft.drawFastHLine(0, 80, W, TFT_DARKGREY);

  // Outdoor
  label(90, outdoorLabel);
  snprintf(buf, sizeof(buf), "%.1f %s", outdoorTemp, tempUnit);
  value(104, 4, buf);
  snprintf(buf, sizeof(buf), "%s%.*f %s", pressureLabel,
           (int)pressureDecimals, pressure, pressureUnit);
  label(144, buf);
  tft.drawFastHLine(0, 160, W, TFT_DARKGREY);

  // Rain
  label(170, rainLabel);
  if (isRaining) {
    tft.fillCircle(W - 18, 174, 6, TFT_CYAN);
  }
  snprintf(buf, sizeof(buf), "1h:  %.*f %s", (int)rainDecimals, rain1h, rainUnit);
  value(186, 2, buf);
  snprintf(buf, sizeof(buf), "24h: %.*f %s", (int)rainDecimals, rain24h, rainUnit);
  value(212, 2, buf);
}

} // namespace TftUI

#endif // ESP32C6_ZERO_TFT
