// Accelerometer-based orientation detection for the Waveshare ESP32-S3-LCD-2.8.
// Reads the QMI8658 IMU and reports a panel rotation 0-3 after debouncing.
// Same chip and I2C address as the ESP32-C6 Touch LCD 1.47 sibling board
// (see project_waveshare_touch_lcd_pinout memory); shared implementation in
// src/orientation.cpp. This board's QMI8658 is mounted rotated 90° relative
// to the C6 board, so rotationFromAccel() swaps the X/Y axis roles for this
// macro — confirmed on real hardware 2026-08-05.

#pragma once
#ifdef WAVESHARE_ESP32S3_LCD

#include <stdint.h>

namespace Orientation {

// Sentinel returned by poll() when no committed change has happened.
static const int8_t UNCHANGED = -1;

// Initialize the QMI8658 over I2C. Wire must already be begun on the shared
// I2C bus (SDA=11, SCL=10 — see I2C_Driver.h in the Waveshare demo). Returns
// true on success.
bool init();

// Poll the accelerometer once. Returns the new panel rotation (0-3)
// when the orientation has been stable long enough to commit a change,
// or UNCHANGED otherwise.
//
// Rotations follow Arduino_GFX convention:
//   0 = portrait, USB on the right side
//   1 = landscape, USB at the bottom
//   2 = portrait, USB on the left side
//   3 = landscape, USB at the top
int8_t poll();

} // namespace Orientation

#endif
