// LVGL UI for the Waveshare ESP32-C6 Touch LCD 1.47 target.
// Public API used by main.cpp. Implementation in src/lvgl_ui.cpp.

#pragma once
#ifdef WAVESHARE_ESP32C6_LCD

#include <stdint.h>
#include <Arduino.h>

namespace LvglUI {

// Hardware + LVGL init + widget tree build. Call once from setup().
// After this returns, the backlight is on and the dashboard is rendered
// (with placeholder values) underneath a hidden modal overlay.
void init();

// Switch the dashboard between landscape (320x172) and portrait (172x320)
// layouts. Rotates the panel, updates LVGL display dimensions, and rebuilds
// the entire widget tree. Caller must re-trigger setHeader/setIndoor/etc.
// afterwards to repopulate values; the new widgets start with placeholders.
// No-op if the requested orientation is already current.
void setOrientation(bool landscape);

// Call from loop() to let LVGL process timers and animations.
void tick();

// Register a callback fired once per touch tap (press + release).
// Pass nullptr to disable. Replaces any previously-registered callback.
void setOnTap(void (*callback)());

// Modal overlays (hide the dashboard while shown).
void showBootSplash(const char* version, const char* date, const char* commit);
void showConnecting(const char* hint, const char* ssid);
void showLocale(const char* name, const char* code);
void showError(const char* title, const char* detail, const char* retrying);

// Update individual dashboard regions; hides the modal as a side effect.
void setHeader(const char* city, const char* localeCode);
void setIndoor(const char* label, const char* humidityLabel,
               float tempDisp, int humidity, const char* tempUnit);
void setOutdoor(const char* label, const char* pressureLabel, const char* pressureUnit,
                float tempDisp, float pressureDisp, uint8_t pressureDecimals,
                const char* tempUnit);
void setRain(const char* label, const char* unit, uint8_t decimals,
             float rain1hDisp, float rain24hDisp, bool isRaining);

} // namespace LvglUI

#endif
