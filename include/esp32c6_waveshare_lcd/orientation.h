// Accelerometer-based orientation detection for ESP32-C6 Touch LCD.
// Reads the QMI8658 IMU and reports a panel rotation 0-3 after debouncing.
// Pin assignments and I2C addr verified against Waveshare 02_qmi8658_output example.

#pragma once
#ifdef WAVESHARE_ESP32C6_LCD

#include <stdint.h>

namespace Orientation {

// Sentinel returned by poll() when no committed change has happened.
static const int8_t UNCHANGED = -1;

// Initialize the QMI8658 over I2C. Wire must already be begun on the
// shared touch I2C bus (SDA=18, SCL=19). Returns true on success.
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
