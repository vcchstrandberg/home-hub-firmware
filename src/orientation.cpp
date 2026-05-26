#ifdef WAVESHARE_ESP32C6_LCD

#include "orientation.h"
#include <Arduino.h>
#include <Wire.h>
#include <FastIMU.h>
#include <math.h>

// QMI8658 lives on the shared touch I2C bus at 0x6B (per Waveshare schematic).
static const uint8_t IMU_ADDRESS = 0x6B;
// Stable for at least this long before we accept the new orientation.
static const unsigned long DEBOUNCE_MS = 600;
// Threshold below which we treat the dominant axis as "not dominant" — when
// the device is roughly flat (Z dominant) we don't change anything.
static const float FLAT_Z_THRESHOLD = 0.85f;

static QMI8658 s_imu;
static calData s_calib = { 0 };
static bool    s_initialized = false;

static int8_t s_committed     = Orientation::UNCHANGED;
static int8_t s_pending       = Orientation::UNCHANGED;
static unsigned long s_pendingSince = 0;

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

// Map the accelerometer reading to one of the four Arduino_GFX rotations.
// Empirical mapping for the Waveshare ESP32-C6-Touch-LCD-1.47 (QMI8658 mount):
//   accelX > 0, |X| > |Y|  → rot 1  (landscape, cable bottom)
//   accelX < 0, |X| > |Y|  → rot 3  (landscape, cable top)
//   accelY > 0, |Y| > |X|  → rot 2  (portrait, cable top)
//   accelY < 0, |Y| > |X|  → rot 0  (portrait, cable bottom)
// The Y-sign mapping was inverted in the first cut (both portrait orientations
// rendered upside down); flipped here after testing on a real device.
static int8_t rotationFromAccel(float ax, float ay) {
  if (fabsf(ax) > fabsf(ay)) {
    return (ax > 0) ? 1 : 3;
  } else {
    return (ay > 0) ? 2 : 0;
  }
}

int8_t Orientation::poll() {
  if (!s_initialized) return UNCHANGED;

  AccelData a;
  s_imu.update();
  s_imu.getAccel(&a);

  // Lying flat → don't change orientation.
  if (fabsf(a.accelZ) > FLAT_Z_THRESHOLD) {
    s_pending = UNCHANGED;
    return UNCHANGED;
  }

  int8_t candidate = rotationFromAccel(a.accelX, a.accelY);

  if (candidate != s_pending) {
    s_pending = candidate;
    s_pendingSince = millis();
    return UNCHANGED;
  }

  if (candidate != s_committed && (millis() - s_pendingSince) >= DEBOUNCE_MS) {
    s_committed = candidate;
    return candidate;
  }

  return UNCHANGED;
}

#endif
