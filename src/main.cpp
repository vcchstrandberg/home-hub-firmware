#include <Arduino.h>
#include <ArduinoJson.h>
#include "arduino_secrets.h"

// ── Platform selection ────────────────────────────────────────────────────────
// WAVESHARE_ESP32C6_LCD  — Waveshare ESP32-C6 Touch LCD 1.47 (integrated TFT)
// ESP32C6_ZERO_TFT       — Waveshare ESP32-C6-Zero + external 2.4" ILI9341 SPI TFT
// ESP32C6_ZERO           — Waveshare ESP32-C6-Zero + external SSD1306 (I2C GPIO6/7)
// ESP32CAM               — AI-Thinker ESP32-CAM + external SSD1306 (I2C GPIO14/15)
// ESP32                  — generic ESP32 DevKit + external SSD1306 (I2C GPIO21/22)
// (neither)              — Arduino Uno R4 WiFi + external SSD1306 (I2C)
//
// Devices call the local Raspberry Pi proxy over plain HTTP — no TLS, no OAuth.

#ifdef WAVESHARE_ESP32C6_LCD
#  include "lvgl_ui.h"
#  include "orientation.h"
#  include <WiFi.h>
#  include <HTTPClient.h>
#  define BUTTON_PIN 9
#elif defined(ESP32C6_ZERO_TFT)
#  include "tft_ui.h"
#  include <WiFi.h>
#  include <HTTPClient.h>
#  define BUTTON_PIN 9
#elif defined(ESP32C6_ZERO)
#  include <U8g2lib.h>
#  include <Wire.h>
#  include <WiFi.h>
#  include <HTTPClient.h>
#  define BUTTON_PIN 9
#elif defined(ESP32)
#  include <U8g2lib.h>
#  include <Wire.h>
#  include <WiFi.h>
#  include <HTTPClient.h>
#  define BUTTON_PIN 0
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
#ifdef WAVESHARE_ESP32C6_LCD
// LVGL widget objects are owned by lvgl_ui.cpp; no globals needed here.
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

#if !defined(WAVESHARE_ESP32C6_LCD) && !defined(ESP32C6_ZERO_TFT) && !defined(NO_DISPLAY)
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
#ifdef WAVESHARE_ESP32C6_LCD
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
#ifdef WAVESHARE_ESP32C6_LCD
void renderForecast();
void onSwipe(int dir);
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

#ifdef WAVESHARE_ESP32C6_LCD
  LvglUI::init();
  // Swipe left/right cycles the three pages; long-press cycles the locale.
  LvglUI::setOnSwipe(onSwipe);
  LvglUI::setOnLongPress(cycleLocale);
  LvglUI::setAbout("Netatmo Home Hub", APP_VERSION, GIT_COMMIT, __DATE__,
                   "https://github.com/vcchstrandberg/home-hub-firmware");
  // Wire is already begun by LvglUI::init() for the touch controller — the
  // QMI8658 shares that I2C bus, so we can init it here without a second
  // Wire.begin().
  Orientation::init();
  LvglUI::showBootSplash(APP_VERSION, __DATE__, GIT_COMMIT);
  // Pump LVGL during the splash so the screen actually paints.
  unsigned long splashUntil = millis() + 5000;
  while (millis() < splashUntil) { LvglUI::tick(); delay(20); }

#elif defined(ESP32C6_ZERO_TFT)
  TftUI::init();
  TftUI::showBootSplash(APP_VERSION, __DATE__, GIT_COMMIT);
  delay(5000);

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

#ifdef WAVESHARE_ESP32C6_LCD
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

  fetchWeatherData();
  g_lastFetch      = millis();
  g_lastCardSwitch = millis();
}

// ── showLocale() ──────────────────────────────────────────────────────────────
void showLocale()
{
#ifdef WAVESHARE_ESP32C6_LCD
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
#ifdef WAVESHARE_ESP32C6_LCD
  renderForecast();  // re-localize the forecast page too
#endif
}

// ── loop() ────────────────────────────────────────────────────────────────────
void loop()
{
  unsigned long now = millis();

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

#ifdef WAVESHARE_ESP32C6_LCD
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
      // The rebuild recreated both pages fresh — repopulate the forecast and
      // restore whichever page was showing before the flip.
      renderForecast();
      LvglUI::showPage(g_page);
    }
  }
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

#ifdef WAVESHARE_ESP32C6_LCD
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

#ifdef WAVESHARE_ESP32C6_LCD
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
#ifdef WAVESHARE_ESP32C6_LCD
  renderForecast();
#endif
}

// ── showError() ───────────────────────────────────────────────────────────────
void showError(const char* title, const char* detail)
{
#ifdef WAVESHARE_ESP32C6_LCD
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
// Shared by the two full-dashboard TFT targets (Waveshare LCD + ILI9341).
#if defined(WAVESHARE_ESP32C6_LCD) || defined(ESP32C6_ZERO_TFT)
static const float HIGH_PRESSURE_HPA = 1020.0f;
#endif

#ifdef WAVESHARE_ESP32C6_LCD

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

// Swipe handler: cycle the pages. dir -1 = swipe left (next), +1 = right (prev).
void onSwipe(int dir)
{
  if (dir < 0) g_page = (g_page + 1) % PAGE_COUNT;
  else         g_page = (uint8_t)((g_page + PAGE_COUNT - 1) % PAGE_COUNT);
  LvglUI::showPage(g_page);
}

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
