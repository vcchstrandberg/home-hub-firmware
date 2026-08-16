// LVGL UI for the Waveshare ESP32-S3-LCD-2.8 target (non-touch variant).
// Public API used by main.cpp. Implementation in src/lvgl_ui_s3.cpp.
//
// Unlike the ESP32-C6 Touch LCD 1.47 sibling, this board has no touch — but
// since 2026-08-16 an external KY-040 rotary encoder (wired to the board's
// spare header pins) drives the same kind of page carousel the C6 gets from
// swiping: four full-screen pages (Inside / Outside+Rain / Weather forecast /
// About+QR), rotation cycles pages, the encoder's built-in push button
// cycles locale. See main.cpp's HAS_ENCODER block for the input handling.

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

// Show page 0..3 (Inside / Outside / Weather / About). Updates the
// page-indicator dots. The pages share the screen; only one is visible at
// a time. Driven by the rotary encoder in main.cpp — see onSwipe() there.
void showPage(uint8_t page);

// Populate the About page (static info; call once after init). url is
// encoded as the QR code.
void setAbout(const char* name, const char* version, const char* commit,
              const char* date, const char* url);

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
