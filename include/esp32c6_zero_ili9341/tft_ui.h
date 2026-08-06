// Color-TFT UI for Waveshare ESP32-C6-Zero + 2.4" ILI9341 SPI display.
// Mirrors the subset of the LvglUI interface that main.cpp needs, but renders
// directly with LovyanGFX. Like the Waveshare LCD, this is a full-dashboard
// display (all weather data on screen at once), so there is no card cycling.
//
// Compiled only when ESP32C6_ZERO_TFT is defined; a no-op otherwise.
#pragma once

#include <stdint.h>

namespace TftUI {

// Panel + SPI bus init, plus a one-time full-panel wipe. Call once from setup().
void init();

// Modal boot/status screens (each clears the panel and shows centered text).
// updatedAt/updateMethod are this board's OTA bookkeeping (see
// checkForFirmwareUpdate() in main.cpp) — when this firmware was last
// flashed and whether that was OTA or USB.
void showBootSplash(const char* version, const char* date, const char* commit,
                    const char* updatedAt, const char* updateMethod);
void showConnecting(const char* hint, const char* ssid);
void showLocale(const char* name, const char* code);
void showError(const char* title, const char* detail, const char* retrying);

// Full weather dashboard — redraws the whole screen with current values.
// `outdoorLabel` is already resolved by the caller (city name when known,
// otherwise the locale's "OUTDOOR" string).
// `highPressure` shows a small sun beside the outdoor temperature (barometric high).
void drawDashboard(const char* indoorLabel, const char* humidityLabel,
                   float indoorTemp, int indoorHumidity, const char* tempUnit,
                   const char* outdoorLabel, const char* pressureLabel,
                   const char* pressureUnit, float outdoorTemp, float pressure,
                   uint8_t pressureDecimals, bool highPressure,
                   const char* rainLabel, const char* rainUnit,
                   uint8_t rainDecimals, float rain1h, float rain24h,
                   bool isRaining,
                   // Tomorrow's forecast. fcTempMax/Min and fcPrecip are already
                   // unit-converted by the caller; fcSymbol is the raw met.no
                   // symbol_code. When fcHasData is false, fcNa is shown instead.
                   bool fcHasData, const char* fcTitle, const char* fcSymbol,
                   float fcTempMax, float fcTempMin, float fcPrecip,
                   const char* fcNa);

} // namespace TftUI
