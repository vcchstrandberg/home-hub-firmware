// Accelerometer-based orientation detection for ESP32-C6 Touch LCD.
// Reads the QMI8658 IMU and reports LANDSCAPE / PORTRAIT after a debounce.
// Pin assignments and I2C addr verified against Waveshare 02_qmi8658_output example.

#pragma once
#ifdef WAVESHARE_ESP32C6_LCD

namespace Orientation {

enum Mode {
  UNKNOWN = 0,
  LANDSCAPE,
  PORTRAIT,
};

// Initialize the QMI8658 over I2C. Wire must already be begun on the
// shared touch I2C bus (SDA=18, SCL=19). Returns true on success.
bool init();

// Poll the accelerometer once. Returns the current debounced orientation
// if it changed since the last reported value, UNKNOWN otherwise.
// Caller decides how often to call (~250ms is plenty — orientation
// doesn't change rapidly).
Mode poll();

} // namespace Orientation

#endif
