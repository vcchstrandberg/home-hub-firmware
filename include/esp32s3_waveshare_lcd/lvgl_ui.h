// LVGL UI for the Waveshare ESP32-S3-LCD-2.8 target (non-touch variant).
// Public API used by main.cpp. Implementation in src/lvgl_ui_s3.cpp.
//
// Unlike the ESP32-C6 Touch LCD 1.47 sibling, this board has no input that
// makes a multi-page carousel practical (no touch, and the extra physical
// button turned out not to be a good fit for it either) — so there is no
// paging here at all. Everything (current conditions + tomorrow's forecast)
// is laid out on one always-visible screen, using the extra room this
// panel's 240x320 gives over the C6's 172x320. There is also no About page.

#pragma once
#ifdef WAVESHARE_ESP32S3_LCD

#include <stdint.h>
#include <Arduino.h>

namespace LvglUI {

// Hardware + LVGL init + widget tree build. Call once from setup().
// After this returns, the backlight is on and the dashboard is rendered
// (with placeholder values) underneath a hidden modal overlay.
void init();

// Set the panel rotation 0-3 (Arduino_GFX convention).
// 0,2 are portrait (240x320); 1,3 are landscape (320x240). Flipping within
// the same aspect (1<->3 or 0<->2) just calls gfx->setRotation and triggers
// a redraw. Switching aspect (landscape<->portrait) also rebuilds the
// widget tree, after which the caller must re-trigger setIndoor/setOutdoor/
// setRain/setForecast to repopulate values. No-op if the rotation hasn't
// changed.
void setOrientation(uint8_t rotation);

// Call from loop() to let LVGL process timers and animations.
void tick();

// Set display backlight brightness, 0-100 %. Driven by LEDC PWM on the BL pin.
// The hub computes the level (time-of-day dimming) and sends it in /weather.
void setBacklight(uint8_t percent);

// Modal overlays (hide the dashboard while shown). updatedAt/updateMethod are
// this board's OTA bookkeeping (see checkForFirmwareUpdate() in main.cpp) —
// when this firmware was last flashed and whether that was OTA or USB.
// failureLine is non-null/non-empty only when the last OTA attempt failed
// (see recordFirmwareUpdateFailure() in main.cpp) — shown as an extra line
// in a warning color; pass "" or nullptr when there's nothing to report.
void showBootSplash(const char* version, const char* date, const char* commit,
                    const char* updatedAt, const char* updateMethod,
                    const char* failureLine);
void showConnecting(const char* hint, const char* ssid);
void showLocale(const char* name, const char* code);
void showError(const char* title, const char* detail, const char* retrying);
// Shown while an OTA download/flash is actually in progress (between the
// lastOffer guard passing and Update.end() returning) — main.cpp holds this
// up for the duration of checkForFirmwareUpdate() itself, no separate timer.
void showOtaProgress(const char* line1, const char* line2);

// Update individual dashboard regions; hides the modal as a side effect.
void setIndoor(const char* label, const char* humidityLabel,
               float tempDisp, int humidity, const char* tempUnit);
// highPressure shows a small sun icon on the outdoor card (barometric high).
void setOutdoor(const char* label, const char* pressureLabel, const char* pressureUnit,
                float tempDisp, float pressureDisp, uint8_t pressureDecimals,
                const char* tempUnit, bool highPressure);
void setRain(const char* label, const char* unit, uint8_t decimals,
             float rain1hDisp, float rain24hDisp, bool isRaining);

// Populate the forecast card. symbolCode is the raw met.no code (e.g.
// "rainandthunder", "clearsky_day") — mapped internally to a drawn icon.
// When hasData is false, the temps are blanked and naText is shown instead of
// the precipitation line.
void setForecast(const char* title, const char* symbolCode, bool hasData,
                 float tempMaxDisp, float tempMinDisp, const char* tempUnit,
                 const char* rainLabel, float precipDisp, uint8_t rainDecimals,
                 const char* rainUnit, const char* naText);

} // namespace LvglUI

#endif
