#ifdef WAVESHARE_ESP32C6_LCD

#include "orientation.h"
#include <Arduino.h>
#include <Wire.h>
#include <FastIMU.h>
#include <math.h>

// QMI8658 lives on the shared touch I2C bus at 0x6B (per Waveshare schematic).
static const uint8_t IMU_ADDRESS = 0x6B;
// Stable for at least this long before we accept the new orientation.
// Long enough that a quick tilt while reading doesn't flip the layout.
static const unsigned long DEBOUNCE_MS = 600;
// Threshold below which we treat the axis as "not dominant" — i.e. when
// the device is roughly flat (Z dominant) we don't change anything.
static const float FLAT_Z_THRESHOLD = 0.85f;

static QMI8658 s_imu;
static calData s_calib = { 0 };
static bool    s_initialized = false;

static Orientation::Mode s_committed = Orientation::UNKNOWN;
static Orientation::Mode s_pending   = Orientation::UNKNOWN;
static unsigned long     s_pendingSince = 0;

bool Orientation::init() {
  int err = s_imu.init(s_calib, IMU_ADDRESS);
  if (err != 0) {
    Serial.print("QMI8658 init failed: ");
    Serial.println(err);
    s_initialized = false;
    return false;
  }
  s_initialized = true;
  return true;
}

Orientation::Mode Orientation::poll() {
  if (!s_initialized) return UNKNOWN;

  AccelData a;
  s_imu.update();
  s_imu.getAccel(&a);

  float absX = fabsf(a.accelX);
  float absY = fabsf(a.accelY);
  float absZ = fabsf(a.accelZ);

  // Lying flat — don't change orientation, reset any pending switch.
  if (absZ > FLAT_Z_THRESHOLD) {
    s_pending = UNKNOWN;
    return UNKNOWN;
  }

  Mode candidate = (absX > absY) ? LANDSCAPE : PORTRAIT;

  if (candidate != s_pending) {
    s_pending = candidate;
    s_pendingSince = millis();
    return UNKNOWN;
  }

  if (candidate != s_committed && (millis() - s_pendingSince) >= DEBOUNCE_MS) {
    s_committed = candidate;
    return candidate;
  }

  return UNKNOWN;
}

#endif
