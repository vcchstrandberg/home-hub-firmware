// LVGL UI implementation for the Waveshare ESP32-S3-LCD-2.8 (non-touch
// variant — see project memory for the Touch sibling this is derived from).
// Compiled only when WAVESHARE_ESP32S3_LCD is defined; a no-op otherwise.
//
// Differences from the ESP32-C6 Touch LCD 1.47 sibling (src/lvgl_ui.cpp):
//   - Stock ST7789 panel (240x320, no col/row offset) vs. the C6's JD9853
//     variant — Arduino_GFX's built-in ST7789 init is used as-is, no custom
//     register sequence needed.
//   - No touch controller, and no other input source turned out to be a good
//     fit for a swipe substitute — so there is no paging at all here. A
//     single always-visible 2x2 grid shows current conditions (indoor,
//     outdoor, rain) AND tomorrow's forecast simultaneously, using the extra
//     room this 240x320 panel has over the C6's 172x320. No About page.
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
  // Dashboard (indoor, outdoor, rain, forecast — all four visible at once)
  lv_obj_t* indoor_card;
  lv_obj_t* indoor_name;       // "INNE"
  lv_obj_t* indoor_temp;       // big number, e.g. "24.6"
  lv_obj_t* indoor_unit;       // "C"
  lv_obj_t* indoor_humidity;   // "Fukt: 41%"

  lv_obj_t* outdoor_card;
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

  lv_obj_t* fc_card;
  lv_obj_t* fc_title;          // "TOMORROW"
  lv_obj_t* fc_icon;           // container the weather icon is drawn into
  lv_obj_t* fc_hi;             // big high temp (amber)
  lv_obj_t* fc_sep;            // "/"
  lv_obj_t* fc_lo;             // big low temp (blue)
  lv_obj_t* fc_unit;           // "C" / "F"
  lv_obj_t* fc_precip;         // "REGN: 0.4 mm"

  // Modal overlay
  lv_obj_t* modal;
  lv_obj_t* modal_label;
} ui;

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

// Build one temperature card: name label top-left, big number left-center,
// unit beside it, sub label (humidity / pressure) bottom-left. Writes the
// four child widget pointers via the out parameters.
static lv_obj_t* createTempCard(lv_obj_t* parent, int x, int y, int w, int h,
                                uint32_t accentColor, const char* nameText,
                                lv_obj_t** outName, lv_obj_t** outTemp,
                                lv_obj_t** outUnit, lv_obj_t** outSub) {
  lv_obj_t* card = lv_obj_create(parent);
  lv_obj_set_size(card, w, h);
  lv_obj_set_pos(card, x, y);
  styleContainer(card, COL_CARD_BG, accentColor, 1, 5, 6);

  *outName = lv_label_create(card);
  lv_label_set_text(*outName, nameText);
  lv_obj_set_style_text_color(*outName, lv_color_hex(accentColor), 0);
  lv_obj_set_style_text_font(*outName, &lv_font_montserrat_14, 0);
  lv_obj_align(*outName, LV_ALIGN_TOP_LEFT, 0, 0);

  *outTemp = lv_label_create(card);
  lv_label_set_text(*outTemp, "--");
  lv_obj_set_style_text_color(*outTemp, lv_color_white(), 0);
  lv_obj_set_style_text_font(*outTemp, &lv_font_montserrat_36, 0);
  lv_obj_align(*outTemp, LV_ALIGN_LEFT_MID, 0, 4);

  *outUnit = lv_label_create(card);
  lv_label_set_text(*outUnit, "");
  lv_obj_set_style_text_color(*outUnit, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_style_text_font(*outUnit, &lv_font_montserrat_24, 0);
  lv_obj_align_to(*outUnit, *outTemp, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -4);

  *outSub = lv_label_create(card);
  lv_label_set_text(*outSub, "--");
  lv_obj_set_style_text_color(*outSub, lv_color_hex(COL_DETAIL), 0);
  lv_obj_set_style_text_font(*outSub, &lv_font_montserrat_12, 0);
  lv_obj_align(*outSub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  return card;
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

static void iconCircle(lv_obj_t* p, int d, int x, int y, uint32_t color) {
  lv_obj_t* c = lv_obj_create(p);
  lv_obj_set_size(c, d, d);
  lv_obj_set_pos(c, x, y);
  lv_obj_set_style_bg_color(c, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(c, 0, 0);
  lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_all(c, 0, 0);
  lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
}

static void iconRect(lv_obj_t* p, int w, int h, int x, int y, int r, uint32_t color) {
  lv_obj_t* b = lv_obj_create(p);
  lv_obj_set_size(b, w, h);
  lv_obj_set_pos(b, x, y);
  lv_obj_set_style_bg_color(b, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_radius(b, r, 0);
  lv_obj_set_style_pad_all(b, 0, 0);
  lv_obj_clear_flag(b, LV_OBJ_FLAG_SCROLLABLE);
}

static void iconLine(lv_obj_t* p, const lv_point_t* pts, uint16_t n, uint32_t color) {
  lv_obj_t* ln = lv_line_create(p);
  lv_line_set_points(ln, pts, n);
  lv_obj_set_style_line_color(ln, lv_color_hex(color), 0);
  lv_obj_set_style_line_width(ln, 3, 0);
  lv_obj_set_style_line_rounded(ln, true, 0);
}

// A grey cloud: a wide rounded body with two puffs on top.
static void iconCloud(lv_obj_t* p, uint32_t color) {
  iconRect(p, 40, 18, 8, 24, 9, color);
  iconCircle(p, 20, 8, 18, color);
  iconCircle(p, 26, 24, 10, color);
}

// (Re)draw the mapped weather icon into the given 56x56 container.
static void buildIcon(lv_obj_t* cont, FcIcon icon) {
  lv_obj_clean(cont);
  switch (icon) {
    case FC_SUN:
      for (int i = 0; i < 8; i++) iconLine(cont, FC_SUN_RAYS[i], 2, COL_SUN);
      iconCircle(cont, 24, 16, 16, COL_SUN);
      break;
    case FC_PARTLY:
      iconCircle(cont, 18, 4, 4, COL_SUN);
      iconCloud(cont, COL_CLOUD);
      break;
    case FC_CLOUD:
      iconCloud(cont, COL_CLOUD);
      break;
    case FC_RAIN:
      iconCloud(cont, COL_CLOUD);
      for (int i = 0; i < 3; i++) iconLine(cont, FC_RAIN_STREAKS[i], 2, COL_OUTDOOR_ACC);
      break;
    case FC_SNOW:
      iconCloud(cont, COL_CLOUD);
      iconCircle(cont, 6, 15, 46, 0xFFFFFF);
      iconCircle(cont, 6, 25, 46, 0xFFFFFF);
      iconCircle(cont, 6, 35, 46, 0xFFFFFF);
      break;
    case FC_THUNDER:
      iconCloud(cont, COL_CLOUD);
      iconLine(cont, FC_BOLT, 4, COL_SUN);
      break;
    case FC_FOG:
      for (int i = 0; i < 4; i++) iconLine(cont, FC_FOG_LINES[i], 2, COL_CLOUD);
      break;
    default:  // FC_UNKNOWN — a muted cloud placeholder
      iconCloud(cont, COL_MUTED);
      break;
  }
}

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

// Build the forecast card contents: title, icon, hi/lo temps, precip. Centered
// flex column, same content as the C6 sibling's forecast page but embedded in
// a card alongside the other three instead of on its own swipeable page.
static void buildForecastCard(lv_obj_t* card) {
  lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(card, 4, LV_PART_MAIN);

  ui.fc_title = lv_label_create(card);
  lv_label_set_text(ui.fc_title, "TOMORROW");
  lv_obj_set_style_text_color(ui.fc_title, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_style_text_font(ui.fc_title, &lv_font_montserrat_14, 0);

  ui.fc_icon = lv_obj_create(card);
  lv_obj_set_size(ui.fc_icon, 56, 56);
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
  lv_obj_set_style_pad_column(row, 4, LV_PART_MAIN);
  lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

  ui.fc_hi = lv_label_create(row);
  lv_label_set_text(ui.fc_hi, "--");
  lv_obj_set_style_text_color(ui.fc_hi, lv_color_hex(COL_INDOOR_ACC), 0);
  lv_obj_set_style_text_font(ui.fc_hi, &lv_font_montserrat_36, 0);

  ui.fc_sep = lv_label_create(row);
  lv_label_set_text(ui.fc_sep, "/");
  lv_obj_set_style_text_color(ui.fc_sep, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_style_text_font(ui.fc_sep, &lv_font_montserrat_24, 0);

  ui.fc_lo = lv_label_create(row);
  lv_label_set_text(ui.fc_lo, "--");
  lv_obj_set_style_text_color(ui.fc_lo, lv_color_hex(COL_OUTDOOR_ACC), 0);
  lv_obj_set_style_text_font(ui.fc_lo, &lv_font_montserrat_36, 0);

  ui.fc_unit = lv_label_create(row);
  lv_label_set_text(ui.fc_unit, "");
  lv_obj_set_style_text_color(ui.fc_unit, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_style_text_font(ui.fc_unit, &lv_font_montserrat_24, 0);

  ui.fc_precip = lv_label_create(card);
  lv_label_set_text(ui.fc_precip, "--");
  lv_obj_set_style_text_color(ui.fc_precip, lv_color_hex(COL_DETAIL), 0);
  lv_obj_set_style_text_font(ui.fc_precip, &lv_font_montserrat_14, 0);
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

// ─── Dashboard: one always-visible 2x2 grid ───────────────────────────────
// indoor | outdoor
// rain   | forecast
// No paging (this board has no input that makes a swipe substitute
// practical), so everything current-conditions-plus-forecast lives on
// screen at once. The grid is symmetric in W/H, so the same builder works
// for both landscape (320x240) and portrait (240x320).
static void buildDashboard() {
  resetScreen();
  lv_obj_t* scr = lv_scr_act();

  const int W = s_gfx->width();
  const int H = s_gfx->height();
  const int M = 2;   // outer margin
  const int G = 2;   // gap between cells
  const int cellW = (W - 2 * M - G) / 2;
  const int cellH = (H - 2 * M - G) / 2;

  createTempCard(scr, M, M, cellW, cellH, COL_INDOOR_ACC, "INNE",
                 &ui.indoor_name,  &ui.indoor_temp,  &ui.indoor_unit,  &ui.indoor_humidity);
  lv_obj_t* outdoor = createTempCard(scr, M + cellW + G, M, cellW, cellH, COL_OUTDOOR_ACC, "UTE",
                 &ui.outdoor_name, &ui.outdoor_temp, &ui.outdoor_unit, &ui.outdoor_pressure);
  ui.outdoor_sun = createSun(outdoor);
  lv_obj_align(ui.outdoor_sun, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  // Rain card, bottom-left.
  ui.rain_row = lv_obj_create(scr);
  lv_obj_set_size(ui.rain_row, cellW, cellH);
  lv_obj_set_pos(ui.rain_row, M, M + cellH + G);
  styleContainer(ui.rain_row, COL_CARD_BG, COL_RAIN_BG, 1, 5, 6);

  ui.rain_label = lv_label_create(ui.rain_row);
  lv_label_set_text(ui.rain_label, "REGN");
  lv_obj_set_style_text_color(ui.rain_label, lv_color_hex(COL_RAIN_BG), 0);
  lv_obj_set_style_text_font(ui.rain_label, &lv_font_montserrat_14, 0);
  lv_obj_align(ui.rain_label, LV_ALIGN_TOP_LEFT, 0, 0);

  ui.rain_droplet = lv_obj_create(ui.rain_row);
  lv_obj_set_size(ui.rain_droplet, 12, 12);
  styleContainer(ui.rain_droplet, 0xFFFFFF, 0, 0, 6, 0);
  lv_obj_align(ui.rain_droplet, LV_ALIGN_TOP_RIGHT, 0, 2);
  lv_obj_add_flag(ui.rain_droplet, LV_OBJ_FLAG_HIDDEN);

  ui.rain_1h = lv_label_create(ui.rain_row);
  lv_label_set_text(ui.rain_1h, "1h: --");
  lv_obj_set_style_text_color(ui.rain_1h, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.rain_1h, &lv_font_montserrat_24, 0);
  lv_obj_align(ui.rain_1h, LV_ALIGN_LEFT_MID, 0, 4);

  ui.rain_24h = lv_label_create(ui.rain_row);
  lv_label_set_text(ui.rain_24h, "24h: --");
  lv_obj_set_style_text_color(ui.rain_24h, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.rain_24h, &lv_font_montserrat_24, 0);
  lv_obj_align(ui.rain_24h, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  // Forecast card, bottom-right — same card styling as the others.
  ui.fc_card = lv_obj_create(scr);
  lv_obj_set_size(ui.fc_card, cellW, cellH);
  lv_obj_set_pos(ui.fc_card, M + cellW + G, M + cellH + G);
  styleContainer(ui.fc_card, COL_CARD_BG, COL_MUTED, 1, 5, 6);
  buildForecastCard(ui.fc_card);

  createModal(scr, W, H);
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
  buildDashboard();
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
    buildDashboard();
  } else {
    // Same aspect (e.g. 1↔3) — widget tree is reusable; just force a redraw
    // so existing widgets render at the new physical orientation.
    lv_obj_invalidate(lv_scr_act());
  }
}

void LvglUI::tick() {
  lv_timer_handler();
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
  // Re-align unit AFTER the temp text changes — initial alignment was
  // computed against the placeholder "--" width and would otherwise leave
  // the unit overlapping the new digits.
  lv_obj_align_to(ui.indoor_unit, ui.indoor_temp, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -4);
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
  lv_obj_align_to(ui.outdoor_unit, ui.outdoor_temp, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -4);
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
  buildIcon(ui.fc_icon, hasData ? iconForSymbol(symbolCode) : FC_UNKNOWN);

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
