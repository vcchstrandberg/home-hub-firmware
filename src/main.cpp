#include <Arduino.h>
#include <ArduinoJson.h>
#include "arduino_secrets.h"

// ── Platform selection ────────────────────────────────────────────────────────
// WAVESHARE_ESP32C6_LCD  — Waveshare ESP32-C6 Touch LCD 1.47 (integrated TFT)
// WAVESHARE_ESP32S3_LCD  — Waveshare ESP32-S3-LCD-2.8, non-touch (integrated TFT)
// ESP32C6_ZERO_TFT       — Waveshare ESP32-C6-Zero + external 2.4" ILI9341 SPI TFT
// ESP32C6_ZERO           — Waveshare ESP32-C6-Zero + external SSD1306 (I2C GPIO6/7)
// ESP32CAM               — AI-Thinker ESP32-CAM + external SSD1306 (I2C GPIO14/15)
// ESP32                  — generic ESP32 DevKit + external SSD1306 (I2C GPIO21/22)
// (neither)              — Arduino Uno R4 WiFi + external SSD1306 (I2C)
//
// Devices call the local Raspberry Pi proxy over plain HTTP — no TLS, no OAuth.

// OTA (Phases 1/2 — see docs/production-readiness.md) is enabled per ESP32
// target once its Phase 0 two-slot partition table is in place; OTA_ENV_NAME
// is the PlatformIO environment name, used to build the /firmware/<env>/...
// URLs. Not defined for the Uno R4 (different chip family, out of scope) or
// for any ESP32 board that hasn't had its Phase 0 migration done yet.
#ifdef WAVESHARE_ESP32C6_LCD
#  include "lvgl_ui.h"
#  include "orientation.h"
#  include <WiFi.h>
#  include <HTTPClient.h>
#  include <ArduinoOTA.h>
#  include <Update.h>
#  include <Preferences.h>
#  define BUTTON_PIN 9
#  define OTA_ENV_NAME "esp32c6_waveshare_lcd"
#elif defined(WAVESHARE_ESP32S3_LCD)
#  include "lvgl_ui.h"
#  include "orientation.h"
#  include <WiFi.h>
#  include <HTTPClient.h>
#  include <ArduinoOTA.h>   // Phase 1: LAN push from a dev machine
#  include <Update.h>       // Phase 2: device-initiated pull from the proxy
#  include <Preferences.h>  // NVS-backed update bookkeeping (timestamp/method/retry-guard)
#  define BUTTON_PIN 0   // BOOT button — same short-press-cycles-locale role as GPIO9 on the C6 board
#  define OTA_ENV_NAME "esp32s3_waveshare_lcd"
#elif defined(ESP32C6_ZERO_TFT)
#  include "tft_ui.h"
#  include <WiFi.h>
#  include <HTTPClient.h>
#  include <ArduinoOTA.h>
#  include <Update.h>
#  include <Preferences.h>
#  define BUTTON_PIN 9
#  define OTA_ENV_NAME "esp32c6_zero_ili9341"
#elif defined(ESP32C6_ZERO)
#  include <U8g2lib.h>
#  include <Wire.h>
#  include <WiFi.h>
#  include <HTTPClient.h>
#  include <ArduinoOTA.h>
#  include <Update.h>
#  include <Preferences.h>
#  define BUTTON_PIN 9
#  define OTA_ENV_NAME "esp32c6_zero"
#elif defined(ESP32)
#  include <U8g2lib.h>
#  include <Wire.h>
#  include <WiFi.h>
#  include <HTTPClient.h>
#  include <ArduinoOTA.h>
#  include <Update.h>
#  include <Preferences.h>
#  define BUTTON_PIN 0
#  ifdef ESP32CAM
#    define OTA_ENV_NAME "esp32cam"
#  else
#    define OTA_ENV_NAME "esp32dev"
#  endif
#else
#  include <U8g2lib.h>
#  include <Wire.h>
#  include "WiFiS3.h"
#  include "Arduino_LED_Matrix.h"   // onboard 12x8 LED matrix (UNO R4 WiFi)
#  define BUTTON_PIN 7
#  define HAS_LED_MATRIX 1
#  define ABOUT_SCREEN 1            // long-press button → About screen with QR (Uno only)
#endif

#ifdef ABOUT_SCREEN
#  include "qrcode.h"   // ricmoo/QRCode — QR generation for the About screen
#endif

// ── Locale ────────────────────────────────────────────────────────────────────
struct Locale {
  const char* name;
  const char* code;
  const char* indoor;
  const char* outdoor;
  const char* rain;
  const char* humidity;
  const char* pressure;
  const char* temp_unit;
  const char* pressure_unit;
  const char* rain_unit;
  uint8_t     pressure_decimals;
  uint8_t     rain_decimals;
  bool        fahrenheit;
  bool        inhg;
  bool        inches;
  const char* connecting;
  const char* wifi_failed;
  const char* check_creds;
  const char* retrying;
  const char* hub_unreachable;
  const char* tomorrow;         // forecast page title
  const char* forecast_na;      // shown when the hub has no forecast yet
};

static const Locale L_SV_SE = {
  "Svenska",    "sv-SE",
  "INNE",       "UTE",        "REGN",
  "Fukt: ",     "Tryck: ",
  "C",          "hPa",        "mm",
  0, 1, false, false, false,
  "Ansluter WiFi:", "WiFi fel",     "Kontrollera",
  "Forsoker...",    "Hub oatkomlig",
  "IMORGON",        "Ingen prognos"
};
static const Locale L_EN_US = {
  "English US", "en-US",
  "INDOOR",     "OUTDOOR",    "RAIN",
  "Humidity: ", "Pressure: ",
  "F",          "inHg",       "in",
  2, 2, true, true, true,
  "Connecting to WiFi:", "WiFi failed",  "Check credentials",
  "Retrying...",         "Hub unreachable",
  "TOMORROW",            "No forecast"
};
static const Locale L_EN_GB = {
  "English UK", "en-GB",
  "INDOOR",     "OUTDOOR",    "RAIN",
  "Humidity: ", "Pressure: ",
  "C",          "hPa",        "mm",
  0, 1, false, false, false,
  "Connecting to WiFi:", "WiFi failed",  "Check credentials",
  "Retrying...",         "Hub unreachable",
  "TOMORROW",            "No forecast"
};
static const Locale L_FR_FR = {
  "Francais",   "fr-FR",
  "INTERIEUR",  "EXTERIEUR",  "PLUIE",
  "Humidite: ", "Pression: ",
  "C",          "hPa",        "mm",
  0, 1, false, false, false,
  "Connexion WiFi:", "WiFi echoue",   "Ver. identifiants",
  "Reessai...",      "Hub inaccessible",
  "DEMAIN",          "Pas de prevision"
};

static const Locale* const locales[] = { &L_SV_SE, &L_EN_US, &L_EN_GB, &L_FR_FR };
static const uint8_t LOCALE_COUNT = 4;
static uint8_t       g_localeIndex = 0;
static const Locale* g_loc = locales[0];

inline float toDisplayTemp(float c)     { return g_loc->fahrenheit ? c * 9.0f / 5.0f + 32.0f : c; }
inline float toDisplayPressure(float h) { return g_loc->inhg       ? h * 0.02953f              : h; }
inline float toDisplayRain(float mm)    { return g_loc->inches     ? mm * 0.03937f             : mm; }

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

#ifndef ESP32
int status = WL_IDLE_STATUS;
#endif

// ── Display objects ───────────────────────────────────────────────────────────
#if defined(WAVESHARE_ESP32C6_LCD) || defined(WAVESHARE_ESP32S3_LCD)
// LVGL widget objects are owned by lvgl_ui.cpp / lvgl_ui_s3.cpp; no globals needed here.
#elif defined(ESP32C6_ZERO_TFT)
// LovyanGFX panel object is owned by tft_ui.cpp; no globals needed here.
#elif !defined(NO_DISPLAY)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
#endif

// ── Onboard LED matrix (UNO R4 WiFi): WiFi-state smiley ───────────────────────
#ifdef HAS_LED_MATRIX
ArduinoLEDMatrix matrix;

// 12 columns x 8 rows. Happy when WiFi is connected, sad when it isn't.
// Row 0 is the BOTTOM of the matrix on this board, so the faces are authored
// bottom-up: mouth in the low rows, eyes in the high rows.
byte FACE_HAPPY[8][12] = {
  { 0,0,0,1,1,1,1,1,1,0,0,0 },  // smile bottom
  { 0,0,1,0,0,0,0,0,0,1,0,0 },
  { 0,1,0,0,0,0,0,0,0,0,1,0 },  // smile corners up
  { 0,1,0,0,0,0,0,0,0,0,1,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,1,0,0,0,0,0,0,1,0,0 },  // eyes
  { 0,0,1,0,0,0,0,0,0,1,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,0 },
};
byte FACE_SAD[8][12] = {
  { 0,1,0,0,0,0,0,0,0,0,1,0 },  // frown corners down
  { 0,1,0,0,0,0,0,0,0,0,1,0 },
  { 0,0,1,0,0,0,0,0,0,1,0,0 },
  { 0,0,0,1,1,1,1,1,1,0,0,0 },  // frown top
  { 0,0,0,0,0,0,0,0,0,0,0,0 },
  { 0,0,1,0,0,0,0,0,0,1,0,0 },  // eyes
  { 0,0,1,0,0,0,0,0,0,1,0,0 },
  { 0,0,0,0,0,0,0,0,0,0,0,0 },
};

void showFace(bool connected) {
  if (connected) matrix.renderBitmap(FACE_HAPPY, 8, 12);
  else           matrix.renderBitmap(FACE_SAD, 8, 12);
}
#endif

#if !defined(WAVESHARE_ESP32C6_LCD) && !defined(WAVESHARE_ESP32S3_LCD) && !defined(ESP32C6_ZERO_TFT) && !defined(NO_DISPLAY)
static const uint8_t rain_drop_bmp[] PROGMEM = {
    0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0x7E, 0x3C, 0x18,
};
#endif

// ── Weather data ──────────────────────────────────────────────────────────────
float  g_indoorTemp     = 0;
int    g_indoorHumidity = 0;
float  g_airPressure    = 0;
float  g_outdoorTemp    = 0;
float  g_rain1h         = 0;
float  g_rain24h        = 0;
bool   g_isRaining      = false;
bool   g_hasData        = false;
String g_city           = "";

// Next-day forecast (from the hub's nested "forecast" object). g_fcHasData is
// false on older hubs or before the first met.no fetch succeeds.
bool   g_fcHasData      = false;
float  g_fcTempMax      = 0;
float  g_fcTempMin      = 0;
float  g_fcPrecip       = 0;   // mm
String g_fcSymbol       = "";  // raw met.no symbol_code
#ifdef WAVESHARE_ESP32C6_LCD
static const uint8_t PAGE_COUNT = 3;  // dashboard, forecast, about
uint8_t g_page          = 0;   // 0 = dashboard, 1 = forecast, 2 = about
unsigned long g_lastSwipe = 0; // millis() of the last swipe (page change)
const unsigned long PAGE_TIMEOUT_MS = 30000;  // auto-return to dashboard after this
#endif
#ifdef ABOUT_SCREEN
bool          g_aboutMode  = false;  // long-press button shows the About screen
unsigned long g_aboutStart = 0;      // millis() when the About screen was entered
const unsigned long ABOUT_TIMEOUT_MS = 30000;  // auto-return to the carousel after this
static const char* ABOUT_URL = "https://github.com/vcchstrandberg/home-hub-firmware";
#endif

// ── Timing ────────────────────────────────────────────────────────────────────
uint8_t       g_card           = 0;
unsigned long g_lastCardSwitch = 0;
unsigned long g_lastFetch      = 0;
#ifdef OTA_ENV_NAME
unsigned long g_lastOtaCheck = 0;
const unsigned long OTA_CHECK_MS = 86400000UL;  // 24h — OTA Phase 2 version poll
#endif
#if defined(WAVESHARE_ESP32C6_LCD) || defined(WAVESHARE_ESP32S3_LCD)
const unsigned long CARD_MS  = 86400000UL; // effectively never — full dashboard always shown
const unsigned long FETCH_MS = 300000;
#elif defined(ESP32C6_ZERO_TFT)
const unsigned long CARD_MS  = 86400000UL; // effectively never — full dashboard always shown
const unsigned long FETCH_MS = 300000;
#elif defined(ESP32)
const unsigned long CARD_MS  = 5000;
const unsigned long FETCH_MS = 300000;
#else
const unsigned long CARD_MS  = 5000;
const unsigned long FETCH_MS = 60000;
#endif

// ── Forward declarations ──────────────────────────────────────────────────────
void fetchWeatherData();
void parseWeather(const String& json);
void drawCard(uint8_t card);
void showError(const char* title, const char* detail = nullptr);
void showLocale();
void cycleLocale();
#if defined(WAVESHARE_ESP32C6_LCD) || defined(WAVESHARE_ESP32S3_LCD)
void renderForecast();
#endif
#ifdef WAVESHARE_ESP32C6_LCD
void onSwipe(int dir);
#endif
#ifdef OTA_ENV_NAME
void checkForFirmwareUpdate();
static void syncTimeFromNtp();
static void recordFirmwareUpdateInfo(String& outTimestamp, String& outMethod);
// NVS ("Preferences") namespace + keys for update bookkeeping. Shared by
// checkForFirmwareUpdate(), the ArduinoOTA onEnd hook in setup(), and
// recordFirmwareUpdateInfo().
//   ver          last APP_VERSION we recorded an update event for
//   ts           unix time (UTC) of that event, or 0 if NTP never synced
//   method       "OTA" or "USB" for that event
//   pendingOta   set just before a reboot caused by either OTA path (Phase 1
//                push or Phase 2 pull); read once on the next boot to tell
//                an OTA-caused version change apart from a USB reflash,
//                which has no code path to set anything before rebooting —
//                USB is therefore the default when this flag is absent
//   lastOffer    the server-offered version string Phase 2 most recently
//                attempted, whether or not it actually changed APP_VERSION —
//                see the incident note in checkForFirmwareUpdate() below
static const char* FW_PREFS_NS = "fwmeta";
#endif
#ifdef ABOUT_SCREEN
void drawAbout();
void toggleAbout();
#endif

// ── setup() ───────────────────────────────────────────────────────────────────
void setup()
{
  g_loc = locales[g_localeIndex];

  Serial.begin(115200);
  unsigned long serialDeadline = millis() + 3000;
  while (!Serial && millis() < serialDeadline) { ; }
  Serial.println("=== Boot ===");

#ifdef HAS_LED_MATRIX
  matrix.begin();
  showFace(false);  // sad until WiFi connects
#endif

#if defined(WAVESHARE_ESP32C6_LCD) || defined(WAVESHARE_ESP32S3_LCD)
  LvglUI::init();
#ifdef WAVESHARE_ESP32C6_LCD
  // Swipe left/right cycles the three pages; touch long-press cycles the
  // locale. The S3 board has neither touch nor paging — it relies solely on
  // the generic BUTTON_PIN short-press handling below for locale cycling,
  // and shows current conditions + forecast on one always-visible screen.
  LvglUI::setOnSwipe(onSwipe);
  LvglUI::setOnLongPress(cycleLocale);
  LvglUI::setAbout("Netatmo Home Hub", APP_VERSION, GIT_COMMIT, __DATE__,
                   "https://github.com/vcchstrandberg/home-hub-firmware");
#endif
  // Wire is already begun by LvglUI::init() (for the touch controller on the
  // C6 board, or directly for the IMU on the S3 board) — the QMI8658 shares
  // that I2C bus, so we can init it here without a second Wire.begin().
  Orientation::init();
  // The boot splash (both LVGL boards) is shown later, after WiFi connects —
  // it now includes the last-update timestamp/method, which need NTP/NVS
  // that aren't available yet at this point in boot. See the OTA_ENV_NAME
  // block below.

#elif defined(ESP32C6_ZERO_TFT)
  TftUI::init();
  // Boot splash shown later, after WiFi connects — see the OTA_ENV_NAME
  // block below (same reasoning as the LVGL boards above).

#elif !defined(NO_DISPLAY)
#  ifdef ESP32CAM
  Wire.begin(14, 15);
#  elif defined(ESP32C6_ZERO)
  // C6-Zero doesn't break out the variant default I2C pins (23/22),
  // so call Wire.begin() explicitly with broken-out pins (Wire1 defaults).
  Wire.begin(6, 7);
#  else
  Wire.begin();
#  endif
  bool oledOk = oled.begin();
  if (oledOk) {
    oled.clearBuffer();
    oled.setFont(u8g2_font_ncenB08_tr);
    oled.drawStr(0, 12, "Netatmo Home Hub");
    oled.drawStr(0, 28, "v" APP_VERSION);
    oled.drawStr(0, 44, __DATE__);
    oled.drawStr(0, 60, GIT_COMMIT);
    oled.sendBuffer();
    delay(5000);
  } else {
    Serial.println("OLED init failed");
  }
#endif

  pinMode(BUTTON_PIN, INPUT_PULLUP);

#ifndef ESP32
  if (WiFi.status() == WL_NO_MODULE) { Serial.println("WiFi module not found!"); while (true) ; }
  if (WiFi.firmwareVersion() < WIFI_FIRMWARE_LATEST_VERSION)
    Serial.println("WiFi firmware update available");
#endif

#if defined(WAVESHARE_ESP32C6_LCD) || defined(WAVESHARE_ESP32S3_LCD)
  LvglUI::showConnecting(g_loc->connecting, ssid);
  LvglUI::tick();
#elif defined(ESP32C6_ZERO_TFT)
  TftUI::showConnecting(g_loc->connecting, ssid);
#elif !defined(NO_DISPLAY)
  oled.setFont(u8g2_font_ncenB08_tr);
  oled.clearBuffer();
  oled.drawStr(0, 20, g_loc->connecting);
  oled.drawStr(0, 34, ssid);
  oled.sendBuffer();
#endif

#ifdef ESP32
  WiFi.begin(ssid, pass);
  uint8_t wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (++wifiAttempts >= 60) { showError(g_loc->wifi_failed, g_loc->check_creds); break; }
  }
#else
  uint8_t wifiAttempts = 0;
  while (status != WL_CONNECTED) {
    Serial.print("Connecting to: "); Serial.println(ssid);
    status = WiFi.begin(ssid, pass);
    delay(10000);
    if (++wifiAttempts == 3) showError(g_loc->wifi_failed, g_loc->check_creds);
  }
#endif

#ifdef HAS_LED_MATRIX
  showFace(WiFi.status() == WL_CONNECTED);  // happy once we're online
#endif

#ifdef OTA_ENV_NAME
  // No mDNS lookup needed for OTA Phase 1: `pio run --target upload
  // --upload-port <ip>` targets this printed IP directly.
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  // OTA Phase 1 (unauthenticated — same trusted home LAN only; add
  // ArduinoOTA.setPassword() before this is exposed any more broadly). The
  // onEnd hook fires just before ArduinoOTA's own restart, so it lands the
  // same "this reboot came from OTA, not USB" marker checkForFirmwareUpdate()
  // sets for a Phase 2 pull — see recordFirmwareUpdateInfo().
  ArduinoOTA.setHostname(DEVICE_NAME);
  ArduinoOTA.onEnd([]() {
    Preferences p;
    p.begin(FW_PREFS_NS, false);
    p.putBool("pendingOta", true);
    p.end();
  });
  ArduinoOTA.begin();

  // Needed only to timestamp the update-info splash/log below; best-effort.
  syncTimeFromNtp();
  String updatedAt, updateMethod;
  recordFirmwareUpdateInfo(updatedAt, updateMethod);
  Serial.print("Updated: "); Serial.print(updatedAt); Serial.print(" ("); Serial.print(updateMethod); Serial.println(")");

#if defined(WAVESHARE_ESP32C6_LCD) || defined(WAVESHARE_ESP32S3_LCD)
  LvglUI::showBootSplash(APP_VERSION, __DATE__, GIT_COMMIT,
                         updatedAt.c_str(), updateMethod.c_str());
  unsigned long splashUntil = millis() + 5000;
  while (millis() < splashUntil) { LvglUI::tick(); delay(20); }
#elif defined(ESP32C6_ZERO_TFT)
  TftUI::showBootSplash(APP_VERSION, __DATE__, GIT_COMMIT,
                        updatedAt.c_str(), updateMethod.c_str());
  delay(5000);
#endif
  // OLED boards (esp32cam/esp32dev/esp32c6_zero): the splash already ran
  // earlier in setup(), before WiFi — no room on a 128x64 panel for a 5th
  // line, so the "Updated:" Serial line above is the only place this shows.

  // OTA Phase 2 — re-enabled 2026-08-06 after the reboot-loop fix (see the
  // incident note on checkForFirmwareUpdate()): a repeat of the exact same
  // non-resolving server offer is now skipped instead of retried forever,
  // so this is safe to run at boot.
  checkForFirmwareUpdate();
  g_lastOtaCheck = millis();
#endif

  fetchWeatherData();
  g_lastFetch      = millis();
  g_lastCardSwitch = millis();
}

// ── showLocale() ──────────────────────────────────────────────────────────────
void showLocale()
{
#if defined(WAVESHARE_ESP32C6_LCD) || defined(WAVESHARE_ESP32S3_LCD)
  LvglUI::showLocale(g_loc->name, g_loc->code);
  unsigned long until = millis() + 1500;
  while (millis() < until) { LvglUI::tick(); delay(20); }
#elif defined(ESP32C6_ZERO_TFT)
  TftUI::showLocale(g_loc->name, g_loc->code);
  delay(1500);
#elif !defined(NO_DISPLAY)
  oled.clearBuffer();
  oled.setFont(u8g2_font_ncenB08_tr);
  oled.drawStr(0, 12, "Language:");
  oled.setFont(u8g2_font_logisoso16_tr);
  oled.drawStr(0, 38, g_loc->name);
  oled.setFont(u8g2_font_ncenB08_tr);
  oled.drawStr(0, 54, g_loc->code);
  oled.sendBuffer();
  delay(1500);
#endif
}

// ── cycleLocale() ─────────────────────────────────────────────────────────────
void cycleLocale()
{
  g_localeIndex = (g_localeIndex + 1) % LOCALE_COUNT;
  g_loc = locales[g_localeIndex];
  Serial.print("Locale: "); Serial.println(g_loc->code);
  showLocale();
#ifdef ABOUT_SCREEN
  if (g_aboutMode) { drawAbout(); return; }  // stay on About; just restore it
#endif
  if (g_hasData) drawCard(g_card);
#if defined(WAVESHARE_ESP32C6_LCD) || defined(WAVESHARE_ESP32S3_LCD)
  renderForecast();  // re-localize the forecast page too
#endif
}

// ── loop() ────────────────────────────────────────────────────────────────────
void loop()
{
  unsigned long now = millis();

#ifdef OTA_ENV_NAME
  ArduinoOTA.handle();
  if (now - g_lastOtaCheck >= OTA_CHECK_MS) {
    g_lastOtaCheck = now;
    checkForFirmwareUpdate();
  }
#endif

  // Button. On the Uno a long press (>=600 ms) toggles the About screen and
  // fires as soon as the threshold is reached (while still held) for a snappy
  // feel; the release is then swallowed. A short press cycles the locale.
  static bool          btnDown   = false;
  static unsigned long btnStart  = 0;
  static bool          btnLongFired = false;
  bool btnPressed = (digitalRead(BUTTON_PIN) == LOW);
  if (btnPressed && !btnDown) { btnDown = true; btnStart = now; btnLongFired = false; }
#ifdef ABOUT_SCREEN
  if (btnPressed && btnDown && !btnLongFired && now - btnStart >= 600) {
    btnLongFired = true;
    toggleAbout();
  }
#endif
  if (!btnPressed && btnDown) {
    btnDown = false;
    if (!btnLongFired && now - btnStart >= 40) cycleLocale();
  }

#ifdef HAS_LED_MATRIX
  // Keep the onboard smiley in sync with WiFi state (only redraw on change).
  static bool lastConnected = true;  // setup() set happy after connecting
  bool connected = (WiFi.status() == WL_CONNECTED);
  if (connected != lastConnected) {
    lastConnected = connected;
    showFace(connected);
  }
#endif

#if defined(WAVESHARE_ESP32C6_LCD) || defined(WAVESHARE_ESP32S3_LCD)
  // Poll the accelerometer ~4 Hz. The poller does its own debouncing and
  // only returns a committed rotation (0-3) when the orientation has been
  // stable for ~600 ms.
  static unsigned long lastOrient = 0;
  if (now - lastOrient >= 250) {
    lastOrient = now;
    int8_t rot = Orientation::poll();
    if (rot != Orientation::UNCHANGED) {
      Serial.print("Orientation: rotation ");
      Serial.println(rot);
      LvglUI::setOrientation((uint8_t)rot);
      if (g_hasData) drawCard(g_card);
      // The rebuild recreated the widget tree fresh — repopulate the forecast
      // (and, on the C6 board, restore whichever page was showing before the
      // flip; the S3 board has no pages to restore).
      renderForecast();
#ifdef WAVESHARE_ESP32C6_LCD
      LvglUI::showPage(g_page);
#endif
    }
  }

#ifdef WAVESHARE_ESP32C6_LCD
  // Auto-return to the dashboard after inactivity: if we're on the forecast or
  // about page and there's been no swipe for a while, snap back to page 0.
  if (g_page != 0 && now - g_lastSwipe >= PAGE_TIMEOUT_MS) {
    g_page = 0;
    LvglUI::showPage(0);
  }
#endif
#endif

  bool aboutShown = false;
#ifdef ABOUT_SCREEN
  // Auto-return to the carousel after a while if the button isn't pressed again.
  if (g_aboutMode && now - g_aboutStart >= ABOUT_TIMEOUT_MS) {
    g_aboutMode = false;
    g_lastCardSwitch = now;
    if (g_hasData) drawCard(g_card);
  }
  aboutShown = g_aboutMode;   // pause the carousel while the About screen is up
#endif
  if (g_hasData && !aboutShown && now - g_lastCardSwitch >= CARD_MS) {
    g_lastCardSwitch = now;
    // Carousel is 3 cards (indoor/outdoor/rain) + a 4th forecast card when the
    // hub is serving forecast data. (Full-dashboard TFT targets never cycle —
    // their CARD_MS is effectively infinite.)
    uint8_t cardCount = g_fcHasData ? 4 : 3;
    g_card = (g_card + 1) % cardCount;
    drawCard(g_card);
  }

  if (now - g_lastFetch >= FETCH_MS) {
    g_lastFetch = now;
    fetchWeatherData();
  }

#if defined(WAVESHARE_ESP32C6_LCD) || defined(WAVESHARE_ESP32S3_LCD)
  // Pump LVGL frequently so animations and redraws stay smooth.
  for (int i = 0; i < 5; i++) { LvglUI::tick(); delay(20); }
#else
  delay(100);
#endif
}

// ── fetchWeatherData() ────────────────────────────────────────────────────────
void fetchWeatherData()
{
#ifdef ESP32
  HTTPClient http;
  String url = String("http://") + PROXY_HOST + ":" + String(PROXY_PORT) + "/weather";
  http.begin(url);
  http.setTimeout(5000);
  http.addHeader("X-Device-Name", DEVICE_NAME);
  // Stable per-board identifier — the hub uses MAC, not IP, to key its registry.
  http.addHeader("X-Device-Id", WiFi.macAddress());
  // git-describe string (see scripts/version.py) — shown in the hub's device list.
  http.addHeader("X-Device-Firmware", APP_VERSION);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("Proxy HTTP %d\n", code);
    showError(g_loc->hub_unreachable, String(code).c_str());
    http.end();
    g_hasData = false;
    return;
  }
  String json = http.getString();
  http.end();
  parseWeather(json);

#else
  // Uno R4 WiFi — plain HTTP via WiFiClient
  WiFiClient client;
  if (!client.connect(PROXY_HOST, PROXY_PORT)) {
    Serial.println("Proxy connect failed");
    showError(g_loc->hub_unreachable);
    g_hasData = false;
    return;
  }
  client.print("GET /weather HTTP/1.0\r\nHost: ");
  client.print(PROXY_HOST);
  client.print("\r\nX-Device-Name: " DEVICE_NAME);
  client.print("\r\nX-Device-Id: ");
  // WiFiS3 has no String macAddress() overload (unlike the ESP32 WiFi lib),
  // so fill a buffer and format it ourselves — otherwise this target fails to
  // compile and the hub never receives a stable per-board identity.
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  client.print(macStr);
  client.print("\r\nX-Device-Firmware: " APP_VERSION);
  client.print("\r\nConnection: close\r\n\r\n");

  unsigned long t = millis() + 5000;
  while (!client.available() && millis() < t) delay(10);

  String resp;
  while (client.available() && resp.length() < 4096)
    resp += (char)client.read();
  client.stop();

  int j = resp.indexOf('{');
  if (j == -1) { Serial.println("No JSON in proxy response"); g_hasData = false; return; }
  parseWeather(resp.substring(j));
#endif
}

// ── parseWeather() ────────────────────────────────────────────────────────────
void parseWeather(const String& json)
{
  JsonDocument doc;
  if (deserializeJson(doc, json)) { Serial.println("JSON parse failed"); g_hasData = false; return; }

  const char* city = doc["city"];
  if (city) g_city = String(city);

  g_indoorTemp     = doc["indoor_temp"]     | 0.0f;
  g_indoorHumidity = doc["indoor_humidity"] | 0;
  g_airPressure    = doc["pressure"]        | 0.0f;
  g_outdoorTemp    = doc["outdoor_temp"]    | 0.0f;
  g_rain1h         = doc["rain_1h"]         | 0.0f;
  g_rain24h        = doc["rain_24h"]        | 0.0f;
  g_isRaining      = doc["is_raining"]      | false;
  g_hasData        = true;
  g_lastCardSwitch = millis();  // restart card timer so display shows full CARD_MS before switching

  // Nested forecast object — absent on older hubs / before the first met.no
  // fetch, in which case g_fcHasData stays false and the page shows "no forecast".
  JsonObjectConst fc = doc["forecast"];
  if (!fc.isNull()) {
    g_fcHasData = true;
    g_fcTempMax = fc["temp_max"]  | 0.0f;
    g_fcTempMin = fc["temp_min"]  | 0.0f;
    g_fcPrecip  = fc["precip_mm"] | 0.0f;
    const char* sym = fc["symbol_code"];
    g_fcSymbol  = sym ? String(sym) : String("");
  } else {
    g_fcHasData = false;
    if (g_card > 2) g_card = 0;  // don't leave the carousel parked on a now-gone forecast card
  }

#if defined(WAVESHARE_ESP32C6_LCD) || defined(WAVESHARE_ESP32S3_LCD)
  // Hub-computed time-of-day backlight level (0-100). Absent on older hubs —
  // leave brightness untouched in that case.
  int backlight = doc["backlight"] | -1;
  if (backlight >= 0) {
    Serial.print("Backlight: "); Serial.println(backlight);
    LvglUI::setBacklight((uint8_t)backlight);
  }
#endif

  Serial.print("City: "); Serial.println(g_city);
  Serial.print("In: ");   Serial.print(g_indoorTemp);  Serial.print("  Out: "); Serial.println(g_outdoorTemp);
#ifdef ABOUT_SCREEN
  if (g_aboutMode) return;  // data is updated, but don't paint over the About screen
#endif
  drawCard(g_card);
#if defined(WAVESHARE_ESP32C6_LCD) || defined(WAVESHARE_ESP32S3_LCD)
  renderForecast();
#endif
}

#ifdef OTA_ENV_NAME
// Sync wall-clock time over NTP. The only thing in this firmware that needs
// real time is the update-event timestamp below; best-effort, since a failed
// sync (no internet reachable from this WiFi, DNS hiccup) shouldn't block
// boot — it just leaves the previously-recorded timestamp in place.
static void syncTimeFromNtp() {
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5000)) {
    Serial.println("NTP sync failed (no internet?) — update timestamp may be stale");
  }
}

// Detect whether this boot is running firmware not yet recorded (APP_VERSION
// differs from the last-recorded "ver") and, if so, record *when* (now, if
// NTP synced — call syncTimeFromNtp() first) and *how* (OTA vs USB, from the
// pendingOta flag left by whichever path caused this reboot). Always returns
// the currently-recorded info via the out params, so repeated calls across
// ordinary reboots (no new flash) keep reporting the same past event.
static void recordFirmwareUpdateInfo(String& outTimestamp, String& outMethod) {
  Preferences prefs;
  prefs.begin(FW_PREFS_NS, false);

  // Consume pendingOta unconditionally, on every boot — not just inside the
  // version-changed branch below. USB flashing doesn't touch NVS at all, so
  // a flag left set by an OTA event that (for whatever reason) wasn't
  // followed by a boot reaching this code — a crash, a skipped build, a
  // manual USB flash landing in between — would otherwise survive to
  // mis-attribute a later, unrelated update. Reading+clearing it here means
  // it can never survive more than one boot cycle.
  bool wasOta = prefs.getBool("pendingOta", false);
  // Preferences::remove() logs an ESP_LOGE line for a key that's already
  // absent — harmless (most boots have nothing to consume), but looks like a
  // real error in the log, so only call it when there's actually a key there.
  if (prefs.isKey("pendingOta")) prefs.remove("pendingOta");

  if (prefs.getString("ver", "") != String(APP_VERSION)) {
    time_t now = time(nullptr);
    // NTP hasn't synced if this is still near the epoch; 1700000000 ~= Nov
    // 2023, comfortably below any real "now" but well past a default clock.
    unsigned long ts = (now > 1700000000) ? (unsigned long)now : 0;

    prefs.putString("ver", APP_VERSION);
    prefs.putULong("ts", ts);
    prefs.putString("method", wasOta ? "OTA" : "USB");
  }

  unsigned long storedTs = prefs.getULong("ts", 0);
  outMethod = prefs.getString("method", "unknown");
  prefs.end();

  if (storedTs == 0) {
    outTimestamp = "unknown time";
  } else {
    time_t t = (time_t)storedTs;
    struct tm* lt = gmtime(&t);
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", lt);
    outTimestamp = String(buf) + " UTC";
  }
}

// ── checkForFirmwareUpdate() ────────────────────────────────────────────────
// OTA Phase 2: pull-based update over the same proxy /weather already uses.
// GET .../version (plain text, e.g. "2.10"); if it differs from our own
// APP_VERSION, GET .../bin and flash it via Update. Any failure (proxy
// unreachable, no firmware published for this env, bad transfer) is silently
// skipped — same resilience posture as fetchWeatherData(), this just runs
// far less often. On success this function does not return: ESP.restart()
// reboots into the new firmware immediately.
//
// Incident 2026-08-06: a live test briefly left the server offering a
// version string that would never resolve to a match (same binary, relabeled
// purely to trigger a test). Because this ran at every boot before the
// dashboard loaded, the device reboot-looped — applying the "update" (which
// changed nothing), rebooting, seeing the same offer again, and repeating,
// never reaching fetchWeatherData(). Fixed by persisting the last-attempted
// offer (NVS "lastOffer"): if the server is still offering the exact string
// we already tried, skip — no point retrying until the offer actually
// changes. A genuinely new offer always clears/overwrites this, so normal
// updates are unaffected; only a repeat of the *same* non-resolving offer is
// suppressed.
void checkForFirmwareUpdate()
{
  String base = String("http://") + PROXY_HOST + ":" + String(PROXY_PORT) + "/firmware/" + OTA_ENV_NAME;

  HTTPClient verHttp;
  verHttp.begin(base + "/version");
  verHttp.setTimeout(5000);
  int verCode = verHttp.GET();
  if (verCode != 200) {
    if (verCode > 0) { Serial.print("OTA: version check HTTP "); Serial.println(verCode); }
    verHttp.end();
    return;
  }
  String serverVersion = verHttp.getString();
  verHttp.end();
  serverVersion.trim();

  if (serverVersion.length() == 0 || serverVersion == APP_VERSION) return;  // up to date

  Preferences prefs;
  prefs.begin(FW_PREFS_NS, false);
  String lastOffer = prefs.getString("lastOffer", "");
  if (serverVersion == lastOffer) {
    prefs.end();
    return;  // already tried this exact offer once; it didn't resolve — don't retry until it changes
  }
  prefs.putString("lastOffer", serverVersion);
  prefs.end();

  Serial.print("OTA: "); Serial.print(APP_VERSION); Serial.print(" -> "); Serial.println(serverVersion);

  HTTPClient binHttp;
  binHttp.begin(base + "/bin");
  binHttp.setTimeout(30000);
  int binCode = binHttp.GET();
  if (binCode != 200) {
    Serial.print("OTA: bin fetch HTTP "); Serial.println(binCode);
    binHttp.end();
    return;
  }

  int len = binHttp.getSize();
  if (len <= 0) {
    Serial.println("OTA: missing/invalid content length");
    binHttp.end();
    return;
  }
  if (!Update.begin(len)) {
    Serial.print("OTA: Update.begin failed: "); Serial.println(Update.errorString());
    binHttp.end();
    return;
  }

  size_t written = Update.writeStream(*binHttp.getStreamPtr());
  binHttp.end();
  if (written != (size_t)len) {
    Serial.print("OTA: wrote "); Serial.print(written); Serial.print("/"); Serial.println(len);
    Update.abort();
    return;
  }
  if (!Update.end() || !Update.isFinished()) {
    Serial.print("OTA: Update.end failed: "); Serial.println(Update.errorString());
    return;
  }

  Preferences donePrefs;
  donePrefs.begin(FW_PREFS_NS, false);
  donePrefs.putBool("pendingOta", true);
  donePrefs.end();

  Serial.println("OTA: update applied, rebooting");
  Serial.flush();
  ESP.restart();
}
#endif

// ── showError() ───────────────────────────────────────────────────────────────
void showError(const char* title, const char* detail)
{
#if defined(WAVESHARE_ESP32C6_LCD) || defined(WAVESHARE_ESP32S3_LCD)
  LvglUI::showError(title, detail, g_loc->retrying);
  LvglUI::tick();
#elif defined(ESP32C6_ZERO_TFT)
  TftUI::showError(title, detail, g_loc->retrying);
#elif !defined(NO_DISPLAY)
  oled.clearBuffer();
  oled.setFont(u8g2_font_open_iconic_embedded_2x_t);
  oled.drawGlyph(0, 16, 71);
  oled.setFont(u8g2_font_ncenB08_tr);
  oled.drawStr(20, 12, "ERROR");
  oled.drawStr(0, 30, title);
  if (detail) oled.drawStr(0, 44, detail);
  oled.drawStr(0, 58, g_loc->retrying);
  oled.sendBuffer();
#endif
}

// ── drawCard() ────────────────────────────────────────────────────────────────
// OLED boards: Card 0 = indoor, Card 1 = outdoor, Card 2 = rain (cycled)
// C6 TFT:      Full dashboard always — all data on screen simultaneously

// Barometric "high pressure" threshold in hPa. Compared against the raw
// reading (not the display value) so it's the same cutoff in hPa or inHg.
// Shared by the full-dashboard TFT targets (Waveshare LCDs + ILI9341).
#if defined(WAVESHARE_ESP32C6_LCD) || defined(WAVESHARE_ESP32S3_LCD) || defined(ESP32C6_ZERO_TFT)
static const float HIGH_PRESSURE_HPA = 1020.0f;
#endif

#if defined(WAVESHARE_ESP32C6_LCD) || defined(WAVESHARE_ESP32S3_LCD)

void drawCard(uint8_t)  // card argument unused — full dashboard always shown
{
  LvglUI::setIndoor(g_loc->indoor, g_loc->humidity,
                    toDisplayTemp(g_indoorTemp), g_indoorHumidity, g_loc->temp_unit);
  LvglUI::setOutdoor(g_loc->outdoor, g_loc->pressure, g_loc->pressure_unit,
                     toDisplayTemp(g_outdoorTemp), toDisplayPressure(g_airPressure),
                     g_loc->pressure_decimals, g_loc->temp_unit,
                     g_airPressure >= HIGH_PRESSURE_HPA);
  LvglUI::setRain(g_loc->rain, g_loc->rain_unit, g_loc->rain_decimals,
                  toDisplayRain(g_rain1h), toDisplayRain(g_rain24h), g_isRaining);
}

// Push current forecast globals to the forecast page (localized + unit-converted).
void renderForecast()
{
  LvglUI::setForecast(g_loc->tomorrow, g_fcSymbol.c_str(), g_fcHasData,
                      toDisplayTemp(g_fcTempMax), toDisplayTemp(g_fcTempMin), g_loc->temp_unit,
                      g_loc->rain, toDisplayRain(g_fcPrecip), g_loc->rain_decimals,
                      g_loc->rain_unit, g_loc->forecast_na);
}

#ifdef WAVESHARE_ESP32C6_LCD
// Swipe handler: cycle the pages. dir -1 = swipe left (next), +1 = right (prev).
void onSwipe(int dir)
{
  if (dir < 0) g_page = (g_page + 1) % PAGE_COUNT;
  else         g_page = (uint8_t)((g_page + PAGE_COUNT - 1) % PAGE_COUNT);
  g_lastSwipe = millis();
  LvglUI::showPage(g_page);
}
#endif

#elif defined(ESP32C6_ZERO_TFT)

void drawCard(uint8_t)  // card argument unused — full dashboard always shown
{
  TftUI::drawDashboard(
      g_loc->indoor, g_loc->humidity,
      toDisplayTemp(g_indoorTemp), g_indoorHumidity, g_loc->temp_unit,
      g_city.length() > 0 ? g_city.c_str() : g_loc->outdoor,
      g_loc->pressure, g_loc->pressure_unit,
      toDisplayTemp(g_outdoorTemp), toDisplayPressure(g_airPressure),
      g_loc->pressure_decimals, g_airPressure >= HIGH_PRESSURE_HPA,
      g_loc->rain, g_loc->rain_unit, g_loc->rain_decimals,
      toDisplayRain(g_rain1h), toDisplayRain(g_rain24h), g_isRaining,
      g_fcHasData, g_loc->tomorrow, g_fcSymbol.c_str(),
      toDisplayTemp(g_fcTempMax), toDisplayTemp(g_fcTempMin),
      toDisplayRain(g_fcPrecip), g_loc->forecast_na);
}

#elif !defined(NO_DISPLAY)

// Map a met.no symbol_code to an open_iconic_weather_2x glyph. The font has a
// small icon set, so several conditions collapse onto one glyph. Glyphs 64
// (sun) and 67 (rain) are the same ones the outdoor/rain cards already use.
uint8_t oledWeatherGlyph(const char* code)
{
  if (!code || !*code) return 66;  // cloud as a neutral fallback
  String s(code);
  int u = s.lastIndexOf('_');      // strip _day/_night/_polartwilight
  if (u > 0) {
    String suf = s.substring(u + 1);
    if (suf == "day" || suf == "night" || suf == "polartwilight") s = s.substring(0, u);
  }
  if (s.indexOf("thunder") >= 0 || s.indexOf("rain") >= 0 ||
      s.indexOf("sleet")   >= 0 || s.indexOf("snow") >= 0) return 67;  // rain-ish
  if (s == "clearsky" || s == "fair")                      return 64;  // sun
  if (s == "partlycloudy")                                 return 65;  // sun + cloud
  return 66;                                                            // cloud / fog
}

void drawCard(uint8_t card)
{
  oled.clearBuffer();
  switch (card) {
    case 0:
      oled.setFont(u8g2_font_open_iconic_weather_2x_t);
      oled.drawGlyph(0, 16, 69);
      oled.setFont(u8g2_font_ncenB08_tr);
      oled.drawStr(20, 12, g_loc->indoor);
      oled.setFont(u8g2_font_logisoso28_tr);
      oled.drawStr(0, 50, (String(toDisplayTemp(g_indoorTemp), 1) + g_loc->temp_unit).c_str());
      oled.setFont(u8g2_font_ncenB08_tr);
      oled.drawStr(0, 62, (String(g_loc->humidity) + String(g_indoorHumidity) + "%").c_str());
      break;
    case 1:
      oled.setFont(u8g2_font_open_iconic_weather_2x_t);
      oled.drawGlyph(0, 16, 64);
      oled.setFont(u8g2_font_ncenB08_tr);
      oled.drawStr(20, 12, g_city.length() > 0 ? g_city.c_str() : g_loc->outdoor);
      oled.setFont(u8g2_font_logisoso28_tr);
      oled.drawStr(0, 50, (String(toDisplayTemp(g_outdoorTemp), 1) + g_loc->temp_unit).c_str());
      oled.setFont(u8g2_font_ncenB08_tr);
      oled.drawStr(0, 62, (String(g_loc->pressure) + String(toDisplayPressure(g_airPressure), (unsigned int)g_loc->pressure_decimals) + g_loc->pressure_unit).c_str());
      break;
    case 2:
      oled.setFont(u8g2_font_open_iconic_weather_2x_t);
      oled.drawGlyph(0, 16, 67);
      oled.setFont(u8g2_font_ncenB08_tr);
      oled.drawStr(20, 12, g_loc->rain);
      if (g_isRaining) oled.drawXBMP(112, 0, 8, 8, rain_drop_bmp);
      oled.setFont(u8g2_font_logisoso16_tr);
      oled.drawStr(0, 38, ("1h:  " + String(toDisplayRain(g_rain1h),  (unsigned int)g_loc->rain_decimals) + g_loc->rain_unit).c_str());
      oled.drawStr(0, 58, ("24h: " + String(toDisplayRain(g_rain24h), (unsigned int)g_loc->rain_decimals) + g_loc->rain_unit).c_str());
      break;
    case 3:  // Tomorrow's forecast (only reached when g_fcHasData)
      oled.setFont(u8g2_font_open_iconic_weather_2x_t);
      oled.drawGlyph(0, 16, oledWeatherGlyph(g_fcSymbol.c_str()));
      oled.setFont(u8g2_font_ncenB08_tr);
      oled.drawStr(20, 12, g_loc->tomorrow);
      oled.setFont(u8g2_font_logisoso16_tr);
      oled.drawStr(0, 40, (String(toDisplayTemp(g_fcTempMax), 0) + " / " +
                           String(toDisplayTemp(g_fcTempMin), 0) + g_loc->temp_unit).c_str());
      oled.setFont(u8g2_font_ncenB08_tr);
      oled.drawStr(0, 60, (String(g_loc->rain) + ": " +
                           String(toDisplayRain(g_fcPrecip), (unsigned int)g_loc->rain_decimals) +
                           g_loc->rain_unit).c_str());
      break;
  }
  oled.sendBuffer();
}
#else
void drawCard(uint8_t) {}
#endif

#ifdef ABOUT_SCREEN
// About screen (Uno): QR code to the repo on the left, version text on the
// right. The URL fits QR version 3 (29 modules) at ECC low; 2 px/module = 58 px.
void drawAbout()
{
  oled.clearBuffer();

  QRCode qr;
  uint8_t qrData[qrcode_getBufferSize(3)];
  qrcode_initText(&qr, qrData, 3, ECC_LOW, ABOUT_URL);

  const int scale = 2;
  const int qpx   = qr.size * scale;      // 29 * 2 = 58 px
  const int ox    = 2;
  const int oy    = (64 - qpx) / 2;       // vertically centered
  for (uint8_t y = 0; y < qr.size; y++)
    for (uint8_t x = 0; x < qr.size; x++)
      if (qrcode_getModule(&qr, x, y))
        oled.drawBox(ox + x * scale, oy + y * scale, scale, scale);

  const int tx = ox + qpx + 6;            // text column (~66)
  oled.setFont(u8g2_font_ncenB08_tr);
  oled.drawStr(tx, 12, "Netatmo");
  oled.drawStr(tx, 24, "Home Hub");
  oled.drawStr(tx, 44, "v" APP_VERSION);
  oled.drawStr(tx, 58, GIT_COMMIT);
  oled.sendBuffer();
}

// Long-press handler: toggle the About screen, pausing/resuming the carousel.
void toggleAbout()
{
  g_aboutMode = !g_aboutMode;
  if (g_aboutMode) { g_aboutStart = millis(); drawAbout(); }
  else             { g_lastCardSwitch = millis(); drawCard(g_card); }
}
#endif
