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

// Set the panel rotation 0-3 (Arduino_GFX convention).
// 0,2 are portrait (172x320); 1,3 are landscape (320x172). Flipping within
// the same aspect (1<->3 or 0<->2) just calls gfx->setRotation and triggers
// a redraw. Switching aspect (landscape<->portrait) also rebuilds the
// widget tree, after which the caller must re-trigger setIndoor/setOutdoor/
// setRain to repopulate values. No-op if the rotation hasn't changed.
void setOrientation(uint8_t rotation);

// Call from loop() to let LVGL process timers and animations.
void tick();

// Set display backlight brightness, 0-100 %. Driven by LEDC PWM on the BL pin.
// The hub computes the level (time-of-day dimming) and sends it in /weather.
void setBacklight(uint8_t percent);

// Register a callback fired once per touch tap (press + release).
// Pass nullptr to disable. Replaces any previously-registered callback.
void setOnTap(void (*callback)());

// Modal overlays (hide the dashboard while shown).
void showBootSplash(const char* version, const char* date, const char* commit);
void showConnecting(const char* hint, const char* ssid);
void showLocale(const char* name, const char* code);
void showError(const char* title, const char* detail, const char* retrying);

// Update individual dashboard regions; hides the modal as a side effect.
void setIndoor(const char* label, const char* humidityLabel,
               float tempDisp, int humidity, const char* tempUnit);
void setOutdoor(const char* label, const char* pressureLabel, const char* pressureUnit,
                float tempDisp, float pressureDisp, uint8_t pressureDecimals,
                const char* tempUnit);
void setRain(const char* label, const char* unit, uint8_t decimals,
             float rain1hDisp, float rain24hDisp, bool isRaining);

} // namespace LvglUI

#endif
