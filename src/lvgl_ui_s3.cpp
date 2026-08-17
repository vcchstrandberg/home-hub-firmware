// LVGL UI implementation for the Waveshare ESP32-S3-LCD-2.8 (non-touch
// variant — see project memory for the Touch sibling this is derived from).
// Compiled only when WAVESHARE_ESP32S3_LCD is defined; a no-op otherwise.
//
// Differences from the ESP32-C6 Touch LCD 1.47 sibling (src/lvgl_ui.cpp):
//   - Stock ST7789 panel (240x320, no col/row offset) vs. the C6's JD9853
//     variant — Arduino_GFX's built-in ST7789 init is used as-is, no custom
//     register sequence needed.
//   - No touch controller. Until 2026-08-16 that meant no paging at all (no
//     input made a swipe substitute practical) — a single always-visible 2x2
//     grid showed everything at once. An external KY-040 rotary encoder
//     (main.cpp's HAS_ENCODER block) changed that: rotation now drives a
//     four-page carousel — Inside, Outside (temp/pressure + rain), Weather
//     (tomorrow's forecast), About (splash + QR) — one full screen each,
//     mirroring the C6's swipe-driven carousel but with one extra page since
//     rain isn't a fifth always-on card here.
//   - Layout geometry is computed from the panel's actual width/height
//     instead of hardcoded for one fixed resolution.

#ifdef WAVESHARE_ESP32S3_LCD

#include "lvgl_ui.h"
#include <lvgl.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>

// ─── Pin defs (per the official ESP32-S3-LCD-2.8-Demo.zip Arduino example —
// Display_ST7789.h / I2C_Driver.h) ─────────────────────────────────────────
#define PIN_LCD_SCK    40
#define PIN_LCD_MOSI   45
#define PIN_LCD_MISO   -1
#define PIN_LCD_CS     42
#define PIN_LCD_DC     41
#define PIN_LCD_RST    39
#define GFX_BL         5
#define BL_PWM_FREQ    20000
#define BL_PWM_BITS    10
#define BL_PWM_MAX     ((1 << BL_PWM_BITS) - 1)
#define I2C_SDA        11   // shared bus for the QMI8658 IMU (see orientation.cpp)
#define I2C_SCL        10

// ─── Display palette (matches the C6 Touch LCD sibling) ──────────────────
static const uint32_t COL_BG          = 0x0F1419;  // near-black navy
static const uint32_t COL_CARD_BG     = 0x1A2331;  // slightly lighter navy
static const uint32_t COL_RAIN_BG     = 0x0277BD;  // medium blue
static const uint32_t COL_INDOOR_ACC  = 0xFFA726;  // amber
static const uint32_t COL_OUTDOOR_ACC = 0x4FC3F7;  // light blue
static const uint32_t COL_MUTED       = 0x90A4AE;  // blue-gray
static const uint32_t COL_DETAIL      = 0xCFD8DC;  // pale blue-gray
static const uint32_t COL_ERROR_BG    = 0x611A15;  // dark red
static const uint32_t COL_SUN         = 0xFFD54F;  // golden yellow (high-pressure sun)
static const uint32_t COL_CLOUD       = 0xB0BEC5;  // light grey (forecast cloud icon)

// ─── Display hardware ─────────────────────────────────────────────────────
static Arduino_DataBus* s_bus = nullptr;
static Arduino_GFX*     s_gfx = nullptr;

// ─── LVGL display driver glue ─────────────────────────────────────────────
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t*        s_disp_buf = nullptr;
static lv_disp_drv_t      s_disp_drv;

static void disp_flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
#if (LV_COLOR_16_SWAP != 0)
  s_gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t*)&color_p->full, w, h);
#else
  s_gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)&color_p->full, w, h);
#endif
  lv_disp_flush_ready(drv);
}

// ─── Swedish accent stripping (default Montserrat ASCII-only) ────────────
// Multi-byte UTF-8 codepoints above 0x7F have no glyph in the default font.
// Approximate ÅÄÖ → A/A/O for now; a proper Latin-1 font is a follow-up.
static String stripAccents(const char* src) {
  String out;
  if (!src) return out;
  out.reserve(strlen(src));
  for (const unsigned char* p = (const unsigned char*)src; *p; ++p) {
    if (*p == 0xC3 && p[1]) {
      unsigned char c = p[1];
      char r;
      switch (c) {
        case 0x84: r = 'A'; break;  // Ä
        case 0x85: r = 'A'; break;  // Å
        case 0x96: r = 'O'; break;  // Ö
        case 0xA4: r = 'a'; break;  // ä
        case 0xA5: r = 'a'; break;  // å
        case 0xB6: r = 'o'; break;  // ö
        default:   r = '?'; break;
      }
      out += r;
      ++p;
    } else {
      out += (char)*p;
    }
  }
  return out;
}

// ─── Widget pointers ──────────────────────────────────────────────────────
struct {
  // Pages (full-screen containers; one visible at a time) + indicator dots.
  lv_obj_t* page_inside;
  lv_obj_t* page_outside;
  lv_obj_t* page_weather;
  lv_obj_t* page_about;
  lv_obj_t* dots[4];

  // Inside page
  lv_obj_t* indoor_name;       // "INNE"
  lv_obj_t* indoor_temp;       // big number, e.g. "24.6"
  lv_obj_t* indoor_unit;       // "C"
  lv_obj_t* indoor_humidity;   // "Fukt: 41%"

  // Outside page: temp/pressure card on top, rain card below
  lv_obj_t* outdoor_name;
  lv_obj_t* outdoor_temp;
  lv_obj_t* outdoor_unit;
  lv_obj_t* outdoor_pressure;
  lv_obj_t* outdoor_sun;       // golden sun, shown only when pressure is high

  lv_obj_t* rain_row;
  lv_obj_t* rain_label;        // "REGN"
  lv_obj_t* rain_1h;
  lv_obj_t* rain_24h;
  lv_obj_t* rain_droplet;      // small white dot, hidden when not raining

  // Weather (forecast) page
  lv_obj_t* fc_title;          // "TOMORROW"
  lv_obj_t* fc_icon;           // container the weather icon is drawn into
  lv_obj_t* fc_hi;             // big high temp (amber)
  lv_obj_t* fc_sep;            // "/"
  lv_obj_t* fc_lo;             // big low temp (blue)
  lv_obj_t* fc_unit;           // "C" / "F"
  lv_obj_t* fc_precip;         // "REGN: 0.4 mm"

  // About page
  lv_obj_t* about_name;        // "Netatmo Home Hub"
  lv_obj_t* about_version;     // "v2.24"
  lv_obj_t* about_build;       // "abc1234  Aug 16 2026"
  lv_obj_t* about_qr;          // QR code to the repo

  // Modal overlay
  lv_obj_t* modal;
  lv_obj_t* modal_label;
} ui;

// ─── Paging state ──────────────────────────────────────────────────────────
static const uint8_t PAGE_COUNT = 4;   // Inside, Outside, Weather, About
static const int     DOT_BAND   = 14;  // reserved strip at the bottom for the dots
static uint8_t       s_page     = 0;   // 0=Inside 1=Outside 2=Weather 3=About

static String s_abtName    = "Netatmo Home Hub";
static String s_abtVersion = "?";
static String s_abtBuild   = "";
static String s_abtUrl     = "https://github.com/vcchstrandberg/home-hub-firmware";

// ─── UI construction ──────────────────────────────────────────────────────
static void styleContainer(lv_obj_t* obj, uint32_t bgColor, uint32_t borderColor, lv_coord_t border, lv_coord_t radius, lv_coord_t pad) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(bgColor), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_color(obj, lv_color_hex(borderColor), LV_PART_MAIN);
  lv_obj_set_style_border_width(obj, border, LV_PART_MAIN);
  lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
  lv_obj_set_style_pad_all(obj, pad, LV_PART_MAIN);
  lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

// Track current panel rotation (Arduino_GFX convention, 0-3) so setOrientation
// can skip no-op calls and decide whether a widget rebuild is needed.
static uint8_t s_rotation = 1;
static inline bool isLandscapeRotation(uint8_t r) { return r == 1 || r == 3; }

static void resetScreen() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_clean(scr);  // delete previous widget tree
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), LV_PART_MAIN);
  lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

// Sun icon ray geometry, in the 24x24 local frame of the sun container
// (center at 12,12). Eight rays pointing outward. Declared static so the
// pointers handed to lv_line_set_points stay valid for the widget's lifetime.
static const lv_point_t SUN_RAY_PTS[8][2] = {
  {{12, 4},  {12, 0}},   // N
  {{12, 20}, {12, 24}},  // S
  {{20, 12}, {24, 12}},  // E
  {{4, 12},  {0, 12}},   // W
  {{17, 7},  {20, 4}},   // NE
  {{7, 7},   {4, 4}},    // NW
  {{17, 17}, {20, 20}},  // SE
  {{7, 17},  {4, 20}},   // SW
};

// A small sun: golden filled disc surrounded by eight rays, drawn from LVGL
// primitives because the Montserrat symbol font has no sun glyph. Returned
// hidden; setOutdoor un-hides it when the pressure is high. The whole thing is
// one container so the caller can align / show / hide it as a unit.
static lv_obj_t* createSun(lv_obj_t* parent) {
  lv_obj_t* sun = lv_obj_create(parent);
  lv_obj_set_size(sun, 24, 24);
  lv_obj_set_style_bg_opa(sun, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(sun, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(sun, 0, LV_PART_MAIN);
  lv_obj_clear_flag(sun, LV_OBJ_FLAG_SCROLLABLE);

  for (int i = 0; i < 8; i++) {
    lv_obj_t* ray = lv_line_create(sun);
    lv_line_set_points(ray, SUN_RAY_PTS[i], 2);
    lv_obj_set_style_line_color(ray, lv_color_hex(COL_SUN), LV_PART_MAIN);
    lv_obj_set_style_line_width(ray, 2, LV_PART_MAIN);
    lv_obj_set_style_line_rounded(ray, true, LV_PART_MAIN);
  }

  lv_obj_t* disc = lv_obj_create(sun);
  lv_obj_set_size(disc, 12, 12);
  lv_obj_set_style_bg_color(disc, lv_color_hex(COL_SUN), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(disc, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_pad_all(disc, 0, LV_PART_MAIN);
  lv_obj_clear_flag(disc, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(disc);

  lv_obj_add_flag(sun, LV_OBJ_FLAG_HIDDEN);
  return sun;
}

// ─── Forecast weather icon ────────────────────────────────────────────────
// Icons are drawn from LVGL primitives inside a 56x56 container. Line-based
// shapes need their point arrays to outlive the call, so the fixed geometry
// lives in file-scope static const arrays (as with the sun rays above).
enum FcIcon { FC_SUN, FC_PARTLY, FC_CLOUD, FC_RAIN, FC_SNOW, FC_THUNDER, FC_FOG, FC_UNKNOWN };

static const lv_point_t FC_SUN_RAYS[8][2] = {
  {{28, 6},  {28, 0}},   {{28, 50}, {28, 56}},
  {{50, 28}, {56, 28}},  {{6, 28},  {0, 28}},
  {{43, 13}, {47, 9}},   {{13, 13}, {9, 9}},
  {{43, 43}, {47, 47}},  {{13, 43}, {9, 47}},
};
static const lv_point_t FC_RAIN_STREAKS[3][2] = {
  {{18, 42}, {15, 52}}, {{28, 42}, {25, 52}}, {{38, 42}, {35, 52}},
};
static const lv_point_t FC_BOLT[4] = { {30, 40}, {24, 49}, {29, 49}, {23, 56} };
static const lv_point_t FC_FOG_LINES[4][2] = {
  {{10, 16}, {46, 16}}, {{8, 26}, {48, 26}},
  {{12, 36}, {44, 36}}, {{10, 46}, {46, 46}},
};

// Icons are designed at 56x56, then scaled up to fill a real page's worth of
// space (FC_ICON_SCALE/FC_ICON_SIZE below). Line-based shapes need their
// point arrays to outlive the call (lv_line_set_points stores a pointer, not
// a copy) — so the scaled points for each of the up-to-8 line segments a
// single icon needs live in these persistent per-shape buffers, one row per
// segment, not a single shared scratch buffer that a later segment would
// overwrite out from under an earlier one.
static lv_point_t s_sunRaysBuf[8][2];
static lv_point_t s_rainStreaksBuf[3][2];
static lv_point_t s_boltBuf[4];
static lv_point_t s_fogLinesBuf[4][2];

static void scalePts(lv_point_t* dst, const lv_point_t* src, int n, float scale) {
  for (int i = 0; i < n; i++) {
    dst[i].x = (lv_coord_t)(src[i].x * scale + 0.5f);
    dst[i].y = (lv_coord_t)(src[i].y * scale + 0.5f);
  }
}

static void iconCircle(lv_obj_t* p, float scale, int d, int x, int y, uint32_t color) {
  lv_obj_t* c = lv_obj_create(p);
  lv_obj_set_size(c, (int)(d * scale + 0.5f), (int)(d * scale + 0.5f));
  lv_obj_set_pos(c, (int)(x * scale + 0.5f), (int)(y * scale + 0.5f));
  lv_obj_set_style_bg_color(c, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(c, 0, 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
}

static void iconRect(lv_obj_t* p, float scale, int w, int h, int x, int y, int r, uint32_t color) {
  lv_obj_t* b = lv_obj_create(p);
  lv_obj_set_size(b, (int)(w * scale + 0.5f), (int)(h * scale + 0.5f));
  lv_obj_set_pos(b, (int)(x * scale + 0.5f), (int)(y * scale + 0.5f));
  lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_radius(b, (int)(r * scale + 0.5f), 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
}

// pts must already be scaled into one of the persistent buffers above.
static void iconLine(lv_obj_t* p, const lv_point_t* pts, uint16_t n, uint32_t color, int width) {
  lv_obj_t* ln = lv_line_create(p);
  lv_line_set_points(ln, pts, n);
  lv_obj_set_style_line_color(ln, lv_color_hex(color), 0);
  lv_obj_set_style_line_width(ln, width, 0);
  lv_obj_set_style_line_rounded(ln, true, 0);
}

// A grey cloud: a wide rounded body with two puffs on top.
static void iconCloud(lv_obj_t* p, float scale, uint32_t color) {
  iconRect(p, scale, 40, 18, 8, 24, 9, color);
  iconCircle(p, scale, 20, 8, 18, color);
  iconCircle(p, scale, 26, 24, 10, color);
}

// (Re)draw the mapped weather icon into the given container, scaled up from
// the 56x56 design frame the point/rect geometry above was authored at.
static void buildIcon(lv_obj_t* cont, FcIcon icon, float scale) {
  lv_obj_clean(cont);
  int lineW = (int)(3 * scale + 0.5f);
  if (lineW < 2) lineW = 2;
  switch (icon) {
    case FC_SUN:
      for (int i = 0; i < 8; i++) {
        scalePts(s_sunRaysBuf[i], FC_SUN_RAYS[i], 2, scale);
        iconLine(cont, s_sunRaysBuf[i], 2, COL_SUN, lineW);
      }
      iconCircle(cont, scale, 24, 16, 16, COL_SUN);
      break;
    case FC_PARTLY:
      iconCircle(cont, scale, 18, 4, 4, COL_SUN);
      iconCloud(cont, scale, COL_CLOUD);
      break;
    case FC_CLOUD:
      iconCloud(cont, scale, COL_CLOUD);
      break;
    case FC_RAIN:
      iconCloud(cont, scale, COL_CLOUD);
      for (int i = 0; i < 3; i++) {
        scalePts(s_rainStreaksBuf[i], FC_RAIN_STREAKS[i], 2, scale);
        iconLine(cont, s_rainStreaksBuf[i], 2, COL_OUTDOOR_ACC, lineW);
      }
      break;
    case FC_SNOW:
      iconCloud(cont, scale, COL_CLOUD);
      iconCircle(cont, scale, 6, 15, 46, 0xFFFFFF);
      iconCircle(cont, scale, 6, 25, 46, 0xFFFFFF);
      iconCircle(cont, scale, 6, 35, 46, 0xFFFFFF);
      break;
    case FC_THUNDER:
      iconCloud(cont, scale, COL_CLOUD);
      scalePts(s_boltBuf, FC_BOLT, 4, scale);
      iconLine(cont, s_boltBuf, 4, COL_SUN, lineW);
      break;
    case FC_FOG:
      for (int i = 0; i < 4; i++) {
        scalePts(s_fogLinesBuf[i], FC_FOG_LINES[i], 2, scale);
        iconLine(cont, s_fogLinesBuf[i], 2, COL_CLOUD, lineW);
      }
      break;
    default:  // FC_UNKNOWN — a muted cloud placeholder
      iconCloud(cont, scale, COL_MUTED);
      break;
  }
}

// Forecast page icon is drawn 1.5x the 56x56 design frame (84x84) — small
// enough to keep the hand-drawn geometry crisp, large enough to read as the
// page's focal point rather than an accent.
static const float FC_ICON_SCALE = 1.5f;
static const int   FC_ICON_SIZE  = 84;

// Map a met.no symbol_code (day/night suffix tolerated) to an icon category.
// Mirrors the server's _symbol_emoji categories. Order matters: "rainandthunder"
// contains "rain", so thunder is checked first.
static FcIcon iconForSymbol(const char* code) {
  if (!code || !*code) return FC_UNKNOWN;
  String s(code);
  int u = s.lastIndexOf('_');
  if (u > 0) {
    String suf = s.substring(u + 1);
    if (suf == "day" || suf == "night" || suf == "polartwilight") s = s.substring(0, u);
  }
  if (s.indexOf("thunder") >= 0) return FC_THUNDER;
  if (s.indexOf("sleet")   >= 0) return FC_RAIN;   // sleet rendered as rain
  if (s.indexOf("snow")    >= 0) return FC_SNOW;
  if (s.indexOf("rain")    >= 0) return FC_RAIN;
  if (s == "fog")                return FC_FOG;
  if (s == "clearsky")           return FC_SUN;
  if (s == "fair")               return FC_SUN;
  if (s == "partlycloudy")       return FC_PARTLY;
  if (s == "cloudy")             return FC_CLOUD;
  return FC_UNKNOWN;
}

// Build the forecast page contents: title, icon, hi/lo temps, precip —
// centered flex column, sized as a full-page hero rather than a shrunk card.
static void buildForecastCard(lv_obj_t* card) {
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 8, LV_PART_MAIN);

  ui.fc_title = lv_label_create(card);
  lv_label_set_text(ui.fc_title, "TOMORROW");
  lv_obj_set_style_text_color(ui.fc_title, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_style_text_font(ui.fc_title, &lv_font_montserrat_20, 0);

  ui.fc_icon = lv_obj_create(card);
  lv_obj_set_size(ui.fc_icon, FC_ICON_SIZE, FC_ICON_SIZE);
  lv_obj_set_style_bg_opa(ui.fc_icon, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(ui.fc_icon, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(ui.fc_icon, 0, LV_PART_MAIN);
  lv_obj_clear_flag(ui.fc_icon, LV_OBJ_FLAG_SCROLLABLE);

  // Temps row: hi (amber) / lo (blue) unit — separate labels so each is colored.
  lv_obj_t* row = lv_obj_create(card);
  lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(row, 0, LV_PART_MAIN);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, 6, LV_PART_MAIN);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  ui.fc_hi = lv_label_create(row);
  lv_label_set_text(ui.fc_hi, "--");
  lv_obj_set_style_text_color(ui.fc_hi, lv_color_hex(COL_INDOOR_ACC), 0);
  lv_obj_set_style_text_font(ui.fc_hi, &lv_font_montserrat_48, 0);

  ui.fc_sep = lv_label_create(row);
  lv_label_set_text(ui.fc_sep, "/");
  lv_obj_set_style_text_color(ui.fc_sep, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_style_text_font(ui.fc_sep, &lv_font_montserrat_32, 0);

  ui.fc_lo = lv_label_create(row);
  lv_label_set_text(ui.fc_lo, "--");
  lv_obj_set_style_text_color(ui.fc_lo, lv_color_hex(COL_OUTDOOR_ACC), 0);
  lv_obj_set_style_text_font(ui.fc_lo, &lv_font_montserrat_48, 0);

  ui.fc_unit = lv_label_create(row);
  lv_label_set_text(ui.fc_unit, "");
  lv_obj_set_style_text_color(ui.fc_unit, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_style_text_font(ui.fc_unit, &lv_font_montserrat_32, 0);

  ui.fc_precip = lv_label_create(card);
  lv_label_set_text(ui.fc_precip, "--");
  lv_obj_set_style_text_color(ui.fc_precip, lv_color_hex(COL_DETAIL), 0);
  lv_obj_set_style_text_font(ui.fc_precip, &lv_font_montserrat_20, 0);
}

// ─── Modal helpers ───────────────────────────────────────────────────────
static void showModal(const char* text, uint32_t bgColor) {
  lv_obj_set_style_bg_color(ui.modal, lv_color_hex(bgColor), LV_PART_MAIN);
  lv_label_set_text(ui.modal_label, text);
  lv_obj_clear_flag(ui.modal, LV_OBJ_FLAG_HIDDEN);
}

static void hideModal() {
  lv_obj_add_flag(ui.modal, LV_OBJ_FLAG_HIDDEN);
}

// Modal overlay: full-screen, hidden by default. Used for splash, connecting,
// locale-cycle hint, and error messages.
static void createModal(lv_obj_t* parent, int w, int h) {
  ui.modal = lv_obj_create(parent);
  lv_obj_set_size(ui.modal, w, h);
  lv_obj_set_pos(ui.modal, 0, 0);
  styleContainer(ui.modal, COL_BG, 0, 0, 0, 12);

  ui.modal_label = lv_label_create(ui.modal);
  lv_label_set_text(ui.modal_label, "");
  lv_obj_set_style_text_color(ui.modal_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.modal_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(ui.modal_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(ui.modal_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(ui.modal_label, w - 24);
  lv_obj_center(ui.modal_label);

  lv_obj_add_flag(ui.modal, LV_OBJ_FLAG_HIDDEN);
}

// ─── Paging chrome ─────────────────────────────────────────────────────────
// Full-screen page container: solid background, no border/padding.
static lv_obj_t* makePage(lv_obj_t* scr, int w, int h) {
  lv_obj_t* pg = lv_obj_create(scr);
  lv_obj_set_size(pg, w, h);
  lv_obj_set_pos(pg, 0, 0);
  styleContainer(pg, COL_BG, 0, 0, 0, 0);
  return pg;
}

// Four page-indicator dots in the reserved bottom band, drawn on top of the
// pages. The active page's dot is white; the others muted.
static void buildDots(lv_obj_t* scr) {
  for (int i = 0; i < PAGE_COUNT; i++) {
    ui.dots[i] = lv_obj_create(scr);
    lv_obj_set_size(ui.dots[i], 7, 7);
    lv_obj_set_style_bg_opa(ui.dots[i], LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(ui.dots[i], 0, LV_PART_MAIN);
    lv_obj_set_style_radius(ui.dots[i], LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ui.dots[i], 0, LV_PART_MAIN);
    lv_obj_clear_flag(ui.dots[i], LV_OBJ_FLAG_SCROLLABLE);
    int offset = (int)((i - (PAGE_COUNT - 1) / 2.0f) * 12);
    lv_obj_align(ui.dots[i], LV_ALIGN_BOTTOM_MID, offset, -4);
  }
}

// Apply s_page: show the active page, hide the others, recolor the dots. Safe
// to call after a rebuild to restore the previously-selected page.
static void applyPage() {
  lv_obj_t* pages[PAGE_COUNT] = { ui.page_inside, ui.page_outside, ui.page_weather, ui.page_about };
  for (int i = 0; i < PAGE_COUNT; i++) {
    if (!pages[i]) continue;
    if (i == s_page) lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
    else             lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
  }
  for (int i = 0; i < PAGE_COUNT; i++) {
    if (ui.dots[i]) lv_obj_set_style_bg_color(ui.dots[i],
        lv_color_hex(i == s_page ? 0xFFFFFF : COL_MUTED), LV_PART_MAIN);
  }
}

// Bare transparent flex container — the row/column building block every hero
// page below is made of (name label, temp+unit row, sub-stat row, ...).
// Matches the pattern buildForecastCard's temps row already used; pulled out
// here since Inside/Outside now need it repeatedly too.
static lv_obj_t* flexBox(lv_obj_t* parent, lv_flex_flow_t flow,
                         lv_flex_align_t mainPlace, lv_flex_align_t crossPlace,
                         int padGap) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(b, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(b, 0, LV_PART_MAIN);
  lv_obj_set_flex_flow(b, flow);
  lv_obj_set_flex_align(b, mainPlace, crossPlace, LV_FLEX_ALIGN_CENTER);
  if (flow == LV_FLEX_FLOW_ROW) lv_obj_set_style_pad_column(b, padGap, LV_PART_MAIN);
  else                          lv_obj_set_style_pad_row(b, padGap, LV_PART_MAIN);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  return b;
}

// ─── Page 0: Inside — hero-sized indoor reading, full page ───────────────
// Borderless centered composition (same style as the Weather/About pages)
// instead of a small anchored card stretched to fill the screen: the
// temperature is the biggest thing LVGL's compiled-in fonts can render here.
static void buildInsidePage(lv_obj_t* pg) {
  lv_obj_set_flex_flow(pg, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(pg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(pg, 16, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(pg, DOT_BAND, LV_PART_MAIN);

  ui.indoor_name = lv_label_create(pg);
  lv_label_set_text(ui.indoor_name, "INNE");
  lv_obj_set_style_text_color(ui.indoor_name, lv_color_hex(COL_INDOOR_ACC), 0);
  lv_obj_set_style_text_font(ui.indoor_name, &lv_font_montserrat_20, 0);

  lv_obj_t* tempRow = flexBox(pg, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, 6);
  ui.indoor_temp = lv_label_create(tempRow);
  lv_label_set_text(ui.indoor_temp, "--");
  lv_obj_set_style_text_color(ui.indoor_temp, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.indoor_temp, &lv_font_montserrat_48, 0);

  ui.indoor_unit = lv_label_create(tempRow);
  lv_label_set_text(ui.indoor_unit, "");
  lv_obj_set_style_text_color(ui.indoor_unit, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_style_text_font(ui.indoor_unit, &lv_font_montserrat_32, 0);

  ui.indoor_humidity = lv_label_create(pg);
  lv_label_set_text(ui.indoor_humidity, "--");
  lv_obj_set_style_text_color(ui.indoor_humidity, lv_color_hex(COL_DETAIL), 0);
  lv_obj_set_style_text_font(ui.indoor_humidity, &lv_font_montserrat_20, 0);
}

// ─── Page 1: Outside — hero temp/pressure block + a rain block below ─────
// Rain doesn't get its own page (only three content pages were wanted
// alongside About), so it lives here since it's an outdoor condition too.
// Same borderless hero style as Inside; the rain block is visually grouped
// by spacing and an accent color rather than a bordered box.
static void buildOutsidePage(lv_obj_t* pg) {
  lv_obj_set_flex_flow(pg, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(pg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(pg, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(pg, DOT_BAND, LV_PART_MAIN);

  ui.outdoor_name = lv_label_create(pg);
  lv_label_set_text(ui.outdoor_name, "UTE");
  lv_obj_set_style_text_color(ui.outdoor_name, lv_color_hex(COL_OUTDOOR_ACC), 0);
  lv_obj_set_style_text_font(ui.outdoor_name, &lv_font_montserrat_20, 0);

  lv_obj_t* tempRow = flexBox(pg, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, 6);
  ui.outdoor_temp = lv_label_create(tempRow);
  lv_label_set_text(ui.outdoor_temp, "--");
  lv_obj_set_style_text_color(ui.outdoor_temp, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.outdoor_temp, &lv_font_montserrat_48, 0);

  ui.outdoor_unit = lv_label_create(tempRow);
  lv_label_set_text(ui.outdoor_unit, "");
  lv_obj_set_style_text_color(ui.outdoor_unit, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_style_text_font(ui.outdoor_unit, &lv_font_montserrat_32, 0);

  ui.outdoor_sun = createSun(tempRow);  // hidden by default; flex slots it in when shown

  ui.outdoor_pressure = lv_label_create(pg);
  lv_label_set_text(ui.outdoor_pressure, "--");
  lv_obj_set_style_text_color(ui.outdoor_pressure, lv_color_hex(COL_DETAIL), 0);
  lv_obj_set_style_text_font(ui.outdoor_pressure, &lv_font_montserrat_20, 0);

  lv_obj_t* rainBlock = flexBox(pg, LV_FLEX_FLOW_COLUMN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, 4);
  lv_obj_set_style_pad_top(rainBlock, 8, LV_PART_MAIN);

  lv_obj_t* labelRow = flexBox(rainBlock, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, 6);
  ui.rain_label = lv_label_create(labelRow);
  lv_label_set_text(ui.rain_label, "REGN");
  lv_obj_set_style_text_color(ui.rain_label, lv_color_hex(COL_OUTDOOR_ACC), 0);
  lv_obj_set_style_text_font(ui.rain_label, &lv_font_montserrat_20, 0);

  ui.rain_droplet = lv_obj_create(labelRow);
  lv_obj_set_size(ui.rain_droplet, 12, 12);
  styleContainer(ui.rain_droplet, 0xFFFFFF, 0, 0, 6, 0);
  lv_obj_add_flag(ui.rain_droplet, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t* valuesRow = flexBox(rainBlock, LV_FLEX_FLOW_ROW, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, 20);
  ui.rain_1h = lv_label_create(valuesRow);
  lv_label_set_text(ui.rain_1h, "1h: --");
  lv_obj_set_style_text_color(ui.rain_1h, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.rain_1h, &lv_font_montserrat_32, 0);

  ui.rain_24h = lv_label_create(valuesRow);
  lv_label_set_text(ui.rain_24h, "24h: --");
  lv_obj_set_style_text_color(ui.rain_24h, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.rain_24h, &lv_font_montserrat_32, 0);
}

// ─── Page 2: Weather — tomorrow's forecast, full screen ──────────────────
static void buildWeatherPage(lv_obj_t* pg) {
  lv_obj_set_style_pad_bottom(pg, DOT_BAND, LV_PART_MAIN);
  buildForecastCard(pg);
}

// ─── Page 3: About — name/version/build + QR code to the repo ────────────
static void buildAboutPage(lv_obj_t* pg) {
  lv_obj_set_flex_flow(pg, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(pg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(pg, 6, LV_PART_MAIN);
  lv_obj_set_style_pad_all(pg, 8, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(pg, DOT_BAND, LV_PART_MAIN);

  ui.about_name = lv_label_create(pg);
  lv_label_set_text(ui.about_name, s_abtName.c_str());
  lv_obj_set_style_text_color(ui.about_name, lv_color_hex(COL_OUTDOOR_ACC), 0);
  lv_obj_set_style_text_font(ui.about_name, &lv_font_montserrat_14, 0);

  ui.about_version = lv_label_create(pg);
  lv_label_set_text(ui.about_version, (String("v") + s_abtVersion).c_str());
  lv_obj_set_style_text_color(ui.about_version, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.about_version, &lv_font_montserrat_14, 0);

  ui.about_build = lv_label_create(pg);
  lv_label_set_text(ui.about_build, s_abtBuild.c_str());
  lv_obj_set_style_text_color(ui.about_build, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_style_text_font(ui.about_build, &lv_font_montserrat_12, 0);

  // White padded box gives the QR a quiet zone so scanners lock on reliably.
  // Slightly larger than the C6 sibling's since this panel has more room.
  lv_obj_t* qbox = lv_obj_create(pg);
  lv_obj_set_size(qbox, 96, 96);
  lv_obj_set_style_bg_color(qbox, lv_color_white(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(qbox, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(qbox, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(qbox, 4, LV_PART_MAIN);
  lv_obj_set_style_pad_all(qbox, 5, LV_PART_MAIN);
  lv_obj_clear_flag(qbox, LV_OBJ_FLAG_SCROLLABLE);

  ui.about_qr = lv_qrcode_create(qbox, 86, lv_color_black(), lv_color_white());
  lv_obj_center(ui.about_qr);
  lv_qrcode_update(ui.about_qr, s_abtUrl.c_str(), s_abtUrl.length());
}

// ─── Build all four pages ──────────────────────────────────────────────────
// The grid is symmetric in W/H, so the same builder works for both landscape
// (320x240) and portrait (240x320).
static void buildPages() {
  resetScreen();
  lv_obj_t* scr = lv_scr_act();

  const int W = s_gfx->width();
  const int H = s_gfx->height();

  ui.page_inside = makePage(scr, W, H);
  buildInsidePage(ui.page_inside);

  ui.page_outside = makePage(scr, W, H);
  buildOutsidePage(ui.page_outside);

  ui.page_weather = makePage(scr, W, H);
  buildWeatherPage(ui.page_weather);

  ui.page_about = makePage(scr, W, H);
  buildAboutPage(ui.page_about);

  buildDots(scr);
  createModal(scr, W, H);
  applyPage();
}

// ─── Public API ──────────────────────────────────────────────────────────
void LvglUI::init() {
  s_bus = new Arduino_HWSPI(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCK, PIN_LCD_MOSI);
  // Stock ST7789, no col/row offset — unlike the C6 sibling's JD9853, this
  // panel's controller is fully supported by Arduino_GFX's built-in init, so
  // no custom register sequence is needed. ips=true matches the demo's
  // explicit Display Inversion On (cmd 0x21) at the end of its init sequence.
  s_gfx = new Arduino_ST7789(
      s_bus, PIN_LCD_RST, 0 /* rotation */, true /* IPS */,
      240, 320, 0, 0, 0, 0);

  if (!s_gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }
  s_gfx->setRotation(1);             // landscape (matches existing layout)
  s_gfx->fillScreen(0x0000);

  // Backlight on LEDC PWM so brightness is adjustable. Start at full; the
  // first weather fetch hands us the hub's time-of-day level.
  ledcAttach(GFX_BL, BL_PWM_FREQ, BL_PWM_BITS);
  ledcWrite(GFX_BL, BL_PWM_MAX);

  lv_init();

  uint32_t w = s_gfx->width();
  uint32_t h = s_gfx->height();
  uint32_t bufLines = 40;
  uint32_t bufSize  = w * bufLines;

  s_disp_buf = (lv_color_t*)heap_caps_malloc(
      bufSize * sizeof(lv_color_t),
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!s_disp_buf) {
    s_disp_buf = (lv_color_t*)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_8BIT);
  }
  if (!s_disp_buf) {
    Serial.println("LVGL draw buffer alloc failed");
    return;
  }

  lv_disp_draw_buf_init(&s_draw_buf, s_disp_buf, NULL, bufSize);
  lv_disp_drv_init(&s_disp_drv);
  s_disp_drv.hor_res  = w;
  s_disp_drv.ver_res  = h;
  s_disp_drv.flush_cb = disp_flush;
  s_disp_drv.draw_buf = &s_draw_buf;
  lv_disp_drv_register(&s_disp_drv);

  // No touch panel on this board — the QMI8658 IMU (for orientation) still
  // needs the I2C bus.
  Wire.begin(I2C_SDA, I2C_SCL);

  s_rotation = 1;
  buildPages();
}

void LvglUI::setBacklight(uint8_t percent) {
  if (percent > 100) percent = 100;
  ledcWrite(GFX_BL, (uint32_t)percent * BL_PWM_MAX / 100);
}

void LvglUI::setOrientation(uint8_t rotation) {
  if (rotation > 3) return;
  if (rotation == s_rotation) return;

  bool aspectChanged = isLandscapeRotation(rotation) != isLandscapeRotation(s_rotation);
  s_rotation = rotation;
  s_gfx->setRotation(rotation);

  if (aspectChanged) {
    uint32_t w = s_gfx->width();
    uint32_t h = s_gfx->height();
    s_disp_drv.hor_res = w;
    s_disp_drv.ver_res = h;
    lv_disp_drv_update(lv_disp_get_default(), &s_disp_drv);
    buildPages();
  } else {
    // Same aspect (e.g. 1↔3) — widget tree is reusable; just force a redraw
    // so existing widgets render at the new physical orientation.
    lv_obj_invalidate(lv_scr_act());
  }
}

void LvglUI::tick() {
  lv_timer_handler();
}

void LvglUI::showPage(uint8_t page) {
  s_page = (page >= PAGE_COUNT) ? (PAGE_COUNT - 1) : page;
  applyPage();
}

void LvglUI::setAbout(const char* name, const char* version, const char* commit,
                      const char* date, const char* url) {
  if (name && *name)       s_abtName = name;
  if (version && *version) s_abtVersion = version;
  if (url && *url)         s_abtUrl = url;
  s_abtBuild = String(commit ? commit : "") +
               ((commit && *commit && date && *date) ? "  " : "") +
               (date ? date : "");

  // Repopulate if the page already exists (e.g. called after init or a rebuild).
  if (ui.about_name) {
    lv_label_set_text(ui.about_name, s_abtName.c_str());
    lv_label_set_text(ui.about_version, (String("v") + s_abtVersion).c_str());
    lv_label_set_text(ui.about_build, s_abtBuild.c_str());
    if (ui.about_qr) lv_qrcode_update(ui.about_qr, s_abtUrl.c_str(), s_abtUrl.length());
  }
}

void LvglUI::showBootSplash(const char* version, const char* date, const char* commit,
                            const char* updatedAt, const char* updateMethod,
                            const char* failureLine) {
  String t = String("Netatmo Home Hub\nv") + (version ? version : "?") +
             "\n" + (date ? date : "?") + "\n" + (commit ? commit : "?") +
             "\nUpdated " + (updatedAt ? updatedAt : "?") +
             " (" + (updateMethod ? updateMethod : "?") + ")";
  bool failed = failureLine && *failureLine;
  if (failed) { t += "\n"; t += failureLine; }
  showModal(t.c_str(), failed ? COL_ERROR_BG : COL_BG);
}

void LvglUI::showConnecting(const char* hint, const char* ssid) {
  String t = stripAccents(hint) + "\n" + (ssid ? ssid : "?");
  showModal(t.c_str(), COL_BG);
}

void LvglUI::showOtaProgress(const char* line1, const char* line2) {
  String t = String(line1 ? line1 : "") + "\n" + (line2 ? line2 : "");
  showModal(t.c_str(), COL_BG);
}

void LvglUI::showLocale(const char* name, const char* code) {
  String t = String("Language\n") + stripAccents(name) + "\n" + (code ? code : "?");
  showModal(t.c_str(), COL_BG);
}

void LvglUI::showError(const char* title, const char* detail, const char* retrying) {
  String t = String("ERROR\n") + stripAccents(title);
  if (detail && *detail) { t += "\n"; t += detail; }
  if (retrying && *retrying) { t += "\n"; t += stripAccents(retrying); }
  showModal(t.c_str(), COL_ERROR_BG);
}

void LvglUI::setIndoor(const char* label, const char* humidityLabel,
                       float tempDisp, int humidity, const char* tempUnit) {
  hideModal();
  lv_label_set_text(ui.indoor_name, stripAccents(label).c_str());
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", tempDisp);
  lv_label_set_text(ui.indoor_temp, buf);
  lv_label_set_text(ui.indoor_unit, tempUnit ? tempUnit : "");
  // No manual re-align needed here (unlike the old anchored-card layout) —
  // indoor_temp/indoor_unit are flex-row siblings, so LVGL repositions them
  // automatically whenever either label's text (and therefore width) changes.
  String h = stripAccents(humidityLabel) + String(humidity) + "%";
  lv_label_set_text(ui.indoor_humidity, h.c_str());
}

void LvglUI::setOutdoor(const char* label, const char* pressureLabel, const char* pressureUnit,
                        float tempDisp, float pressureDisp, uint8_t pressureDecimals,
                        const char* tempUnit, bool highPressure) {
  hideModal();
  lv_label_set_text(ui.outdoor_name, stripAccents(label).c_str());
  char tbuf[16];
  snprintf(tbuf, sizeof(tbuf), "%.1f", tempDisp);
  lv_label_set_text(ui.outdoor_temp, tbuf);
  lv_label_set_text(ui.outdoor_unit, tempUnit ? tempUnit : "");
  // Flex-row siblings reposition automatically on text-width change (see setIndoor).
  char pbuf[24];
  snprintf(pbuf, sizeof(pbuf), "%.*f", pressureDecimals, pressureDisp);
  String p = stripAccents(pressureLabel) + pbuf + (pressureUnit ? pressureUnit : "");
  lv_label_set_text(ui.outdoor_pressure, p.c_str());
  if (highPressure) lv_obj_clear_flag(ui.outdoor_sun, LV_OBJ_FLAG_HIDDEN);
  else              lv_obj_add_flag(ui.outdoor_sun, LV_OBJ_FLAG_HIDDEN);
}

void LvglUI::setRain(const char* label, const char* unit, uint8_t decimals,
                     float rain1hDisp, float rain24hDisp, bool isRaining) {
  hideModal();
  lv_label_set_text(ui.rain_label, stripAccents(label).c_str());
  char buf1[24], buf2[24];
  snprintf(buf1, sizeof(buf1), "1h: %.*f%s",  decimals, rain1hDisp,  unit ? unit : "");
  snprintf(buf2, sizeof(buf2), "24h: %.*f%s", decimals, rain24hDisp, unit ? unit : "");
  lv_label_set_text(ui.rain_1h, buf1);
  lv_label_set_text(ui.rain_24h, buf2);
  if (isRaining) lv_obj_clear_flag(ui.rain_droplet, LV_OBJ_FLAG_HIDDEN);
  else           lv_obj_add_flag(ui.rain_droplet, LV_OBJ_FLAG_HIDDEN);
}

void LvglUI::setForecast(const char* title, const char* symbolCode, bool hasData,
                         float tempMaxDisp, float tempMinDisp, const char* tempUnit,
                         const char* rainLabel, float precipDisp, uint8_t rainDecimals,
                         const char* rainUnit, const char* naText) {
  if (!ui.fc_title) return;
  lv_label_set_text(ui.fc_title, stripAccents(title).c_str());
  buildIcon(ui.fc_icon, hasData ? iconForSymbol(symbolCode) : FC_UNKNOWN, FC_ICON_SCALE);

  if (hasData) {
    char hi[12], lo[12];
    snprintf(hi, sizeof(hi), "%.0f", tempMaxDisp);
    snprintf(lo, sizeof(lo), "%.0f", tempMinDisp);
    lv_label_set_text(ui.fc_hi,   hi);
    lv_label_set_text(ui.fc_sep,  "/");
    lv_label_set_text(ui.fc_lo,   lo);
    lv_label_set_text(ui.fc_unit, tempUnit ? tempUnit : "");
    char pbuf[32];
    snprintf(pbuf, sizeof(pbuf), "%.*f%s", rainDecimals, precipDisp, rainUnit ? rainUnit : "");
    String p = stripAccents(rainLabel) + pbuf;
    lv_label_set_text(ui.fc_precip, p.c_str());
  } else {
    lv_label_set_text(ui.fc_hi,   "--");
    lv_label_set_text(ui.fc_sep,  "");
    lv_label_set_text(ui.fc_lo,   "");
    lv_label_set_text(ui.fc_unit, "");
    lv_label_set_text(ui.fc_precip, stripAccents(naText).c_str());
  }
}

#endif // WAVESHARE_ESP32S3_LCD
