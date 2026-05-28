// LVGL UI implementation for Waveshare ESP32-C6 Touch LCD 1.47.
// Compiled only when WAVESHARE_ESP32C6_LCD is defined; a no-op otherwise.

#ifdef WAVESHARE_ESP32C6_LCD

#include "lvgl_ui.h"
#include <lvgl.h>
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>
#include <esp_lcd_touch_axs5106l.h>

// ─── Pin defs (Touch variant — see project_waveshare_touch_lcd_pinout memory) ──
#define GFX_BL         23
#define BL_PWM_FREQ    5000
#define BL_PWM_BITS    8
#define BL_PWM_MAX     ((1 << BL_PWM_BITS) - 1)
#define TOUCH_I2C_SDA  18
#define TOUCH_I2C_SCL  19
#define TOUCH_RST      20
#define TOUCH_INT      21

// ─── Display palette ──────────────────────────────────────────────────────────
static const uint32_t COL_BG          = 0x0F1419;  // near-black navy
static const uint32_t COL_HEADER_BG   = 0x024D5C;  // dark teal
static const uint32_t COL_CARD_BG     = 0x1A2331;  // slightly lighter navy
static const uint32_t COL_RAIN_BG     = 0x0277BD;  // medium blue
static const uint32_t COL_INDOOR_ACC  = 0xFFA726;  // amber
static const uint32_t COL_OUTDOOR_ACC = 0x4FC3F7;  // light blue
static const uint32_t COL_MUTED       = 0x90A4AE;  // blue-gray
static const uint32_t COL_DETAIL      = 0xCFD8DC;  // pale blue-gray
static const uint32_t COL_ERROR_BG    = 0x611A15;  // dark red

// ─── Display hardware ─────────────────────────────────────────────────────────
static Arduino_DataBus* s_bus = nullptr;
static Arduino_GFX*     s_gfx = nullptr;

// JD9853 register init — must run before backlight can be turned on.
// Lifted verbatim from the Waveshare 01_gfx_helloworld supplier example.
static void runRegInit() {
  static const uint8_t init_operations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,
    END_WRITE,
    DELAY, 120,
    BEGIN_WRITE,
    WRITE_C8_D16, 0xDF, 0x98, 0x53,
    WRITE_C8_D8, 0xB2, 0x23,
    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 4, 0x00, 0x47, 0x00, 0x6F,
    WRITE_COMMAND_8, 0xBB,
    WRITE_BYTES, 6, 0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,
    WRITE_C8_D16, 0xC0, 0x44, 0xA4,
    WRITE_C8_D8, 0xC1, 0x16,
    WRITE_COMMAND_8, 0xC3,
    WRITE_BYTES, 8, 0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,
    WRITE_COMMAND_8, 0xC4,
    WRITE_BYTES, 12, 0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,
    WRITE_COMMAND_8, 0xC8,
    WRITE_BYTES, 32, 0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00, 0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
    WRITE_COMMAND_8, 0xD0,
    WRITE_BYTES, 5, 0x04, 0x06, 0x6B, 0x0F, 0x00,
    WRITE_C8_D16, 0xD7, 0x00, 0x30,
    WRITE_C8_D8, 0xE6, 0x14,
    WRITE_C8_D8, 0xDE, 0x01,
    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 5, 0x03, 0x13, 0xEF, 0x35, 0x35,
    WRITE_COMMAND_8, 0xC1,
    WRITE_BYTES, 3, 0x14, 0x15, 0xC0,
    WRITE_C8_D16, 0xC2, 0x06, 0x3A,
    WRITE_C8_D16, 0xC4, 0x72, 0x12,
    WRITE_C8_D8, 0xBE, 0x00,
    WRITE_C8_D8, 0xDE, 0x02,
    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3, 0x00, 0x02, 0x00,
    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3, 0x01, 0x02, 0x00,
    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x35, 0x00,
    WRITE_C8_D8, 0x3A, 0x05,
    WRITE_COMMAND_8, 0x2A,
    WRITE_BYTES, 4, 0x00, 0x22, 0x00, 0xCD,
    WRITE_COMMAND_8, 0x2B,
    WRITE_BYTES, 4, 0x00, 0x00, 0x01, 0x3F,
    WRITE_C8_D8, 0xDE, 0x02,
    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3, 0x00, 0x02, 0x00,
    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x36, 0x00,
    WRITE_COMMAND_8, 0x21,
    END_WRITE,
    DELAY, 10,
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29,
    END_WRITE
  };
  s_bus->batchOperation(init_operations, sizeof(init_operations));
}

// ─── LVGL display driver glue ─────────────────────────────────────────────────
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t*        s_disp_buf = nullptr;
static lv_disp_drv_t      s_disp_drv;
static lv_indev_drv_t     s_indev_drv;

// ─── Touch input + tap detection ──────────────────────────────────────────────
// The indev callback runs inside lv_timer_handler(). The user-registered tap
// callback (e.g. cycleLocale) may itself invoke tick() in a busy loop
// (showLocale does this) — invoking it directly here would recurse. So we
// just set a flag in the indev cb and dispatch from tick() after timer_handler
// returns, guarded against re-entry.
static void (*s_onTap)() = nullptr;
static bool          s_wasPressed = false;
static volatile bool s_tapPending = false;

static void touchpad_read_cb(lv_indev_drv_t* /*drv*/, lv_indev_data_t* data) {
  touch_data_t td;
  bsp_touch_read();
  bool nowPressed = bsp_touch_get_coordinates(&td) && td.touch_num > 0;

  if (nowPressed) {
    data->point.x = td.coords[0].x;
    data->point.y = td.coords[0].y;
    data->state   = LV_INDEV_STATE_PRESSED;
  } else {
    data->state   = LV_INDEV_STATE_RELEASED;
  }

  if (s_wasPressed && !nowPressed) s_tapPending = true;
  s_wasPressed = nowPressed;
}

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

// ─── Swedish accent stripping (default Montserrat ASCII-only) ────────────────
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

// ─── Widget pointers ──────────────────────────────────────────────────────────
struct {
  // Dashboard
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

  lv_obj_t* rain_row;
  lv_obj_t* rain_label;        // "REGN"
  lv_obj_t* rain_1h;
  lv_obj_t* rain_24h;
  lv_obj_t* rain_droplet;      // small white dot, hidden when not raining

  // Modal overlay
  lv_obj_t* modal;
  lv_obj_t* modal_label;
} ui;

// ─── UI construction ──────────────────────────────────────────────────────────
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

// Shared helpers used by both layout builders.
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

// ─── Landscape layout (320 x 172) ─────────────────────────────────────────────
// No header — every row is data. Indoor + outdoor cards at top, rain card
// across the bottom (styled like the temp cards: navy bg, teal accent border).
static void buildLandscape() {
  resetScreen();
  lv_obj_t* scr = lv_scr_act();

  createTempCard(scr,  2, 2, 156, 102, COL_INDOOR_ACC,  "INNE",
                 &ui.indoor_name,  &ui.indoor_temp,  &ui.indoor_unit,  &ui.indoor_humidity);
  createTempCard(scr, 162, 2, 156, 102, COL_OUTDOOR_ACC, "UTE",
                 &ui.outdoor_name, &ui.outdoor_temp, &ui.outdoor_unit, &ui.outdoor_pressure);

  // Rain card: spans full width, card-styled to match indoor/outdoor.
  ui.rain_row = lv_obj_create(scr);
  lv_obj_set_size(ui.rain_row, 316, 60);
  lv_obj_set_pos(ui.rain_row, 2, 108);
  styleContainer(ui.rain_row, COL_CARD_BG, COL_RAIN_BG, 1, 5, 6);

  ui.rain_label = lv_label_create(ui.rain_row);
  lv_label_set_text(ui.rain_label, "REGN");
  lv_obj_set_style_text_color(ui.rain_label, lv_color_hex(COL_RAIN_BG), 0);
  lv_obj_set_style_text_font(ui.rain_label, &lv_font_montserrat_14, 0);
  lv_obj_align(ui.rain_label, LV_ALIGN_TOP_LEFT, 0, 0);

  ui.rain_droplet = lv_obj_create(ui.rain_row);
  lv_obj_set_size(ui.rain_droplet, 12, 12);
  styleContainer(ui.rain_droplet, 0xFFFFFF, 0, 0, 6, 0);
  lv_obj_align(ui.rain_droplet, LV_ALIGN_TOP_LEFT, 48, 2);
  lv_obj_add_flag(ui.rain_droplet, LV_OBJ_FLAG_HIDDEN);

  // Big values on the bottom row of the card, 24pt for readability.
  ui.rain_1h = lv_label_create(ui.rain_row);
  lv_label_set_text(ui.rain_1h, "1h: --");
  lv_obj_set_style_text_color(ui.rain_1h, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.rain_1h, &lv_font_montserrat_24, 0);
  lv_obj_align(ui.rain_1h, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  ui.rain_24h = lv_label_create(ui.rain_row);
  lv_label_set_text(ui.rain_24h, "24h: --");
  lv_obj_set_style_text_color(ui.rain_24h, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.rain_24h, &lv_font_montserrat_24, 0);
  lv_obj_align(ui.rain_24h, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

  createModal(scr, 320, 172);
}

// ─── Portrait layout (172 x 320) ──────────────────────────────────────────────
// No header. Indoor + outdoor cards stacked top, rain card at the bottom
// with more vertical room (104 px tall) so values can also be 24pt.
static void buildPortrait() {
  resetScreen();
  lv_obj_t* scr = lv_scr_act();

  createTempCard(scr, 2, 2,   168, 102, COL_INDOOR_ACC,  "INNE",
                 &ui.indoor_name,  &ui.indoor_temp,  &ui.indoor_unit,  &ui.indoor_humidity);
  createTempCard(scr, 2, 108, 168, 102, COL_OUTDOOR_ACC, "UTE",
                 &ui.outdoor_name, &ui.outdoor_temp, &ui.outdoor_unit, &ui.outdoor_pressure);

  // Rain card: full width, card-styled. Three lines (small REGN header,
  // then 1h and 24h values stacked at 24pt).
  ui.rain_row = lv_obj_create(scr);
  lv_obj_set_size(ui.rain_row, 168, 104);
  lv_obj_set_pos(ui.rain_row, 2, 214);
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

  createModal(scr, 172, 320);
}

// ─── Modal helpers ───────────────────────────────────────────────────────────
static void showModal(const char* text, uint32_t bgColor) {
  lv_obj_set_style_bg_color(ui.modal, lv_color_hex(bgColor), LV_PART_MAIN);
  lv_label_set_text(ui.modal_label, text);
  lv_obj_clear_flag(ui.modal, LV_OBJ_FLAG_HIDDEN);
}

static void hideModal() {
  lv_obj_add_flag(ui.modal, LV_OBJ_FLAG_HIDDEN);
}

// ─── Public API ──────────────────────────────────────────────────────────────
void LvglUI::init() {
  s_bus = new Arduino_HWSPI(15 /* DC */, 14 /* CS */, 1 /* SCK */, 2 /* MOSI */);
  s_gfx = new Arduino_ST7789(
      s_bus, 22 /* RST */, 0 /* rotation */, false /* IPS */,
      172, 320, 34, 0, 34, 0);

  if (!s_gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }
  runRegInit();
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

  // Touch input — AXS5106L on its own I2C bus.
  Wire.begin(TOUCH_I2C_SDA, TOUCH_I2C_SCL);
  bsp_touch_init(&Wire, TOUCH_RST, TOUCH_INT, s_gfx->getRotation(), w, h);
  lv_indev_drv_init(&s_indev_drv);
  s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
  s_indev_drv.read_cb = touchpad_read_cb;
  lv_indev_drv_register(&s_indev_drv);

  s_rotation = 1;
  buildLandscape();
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

    if (isLandscapeRotation(rotation)) buildLandscape();
    else                                buildPortrait();
  } else {
    // Same aspect (e.g. 1↔3) — widget tree is reusable; just force a redraw
    // so existing widgets render at the new physical orientation.
    lv_obj_invalidate(lv_scr_act());
  }

  // Touch coordinates may be reported in the old frame until next bsp_touch
  // re-init, but we only use press/release edges (tap detection), not coords,
  // so it doesn't matter.
}

void LvglUI::tick() {
  lv_timer_handler();

  // Dispatch any pending tap from the previous timer handler pass.
  // Re-entry guard: if the user callback itself calls tick() (e.g. via
  // showLocale's busy wait), don't recursively dispatch another tap.
  static bool inDispatch = false;
  if (s_tapPending && s_onTap && !inDispatch) {
    s_tapPending = false;
    inDispatch = true;
    s_onTap();
    inDispatch = false;
  }
}

void LvglUI::setOnTap(void (*callback)()) {
  s_onTap = callback;
}

void LvglUI::showBootSplash(const char* version, const char* date, const char* commit) {
  String t = String("Netatmo Home Hub\nv") + (version ? version : "?") +
             "\n" + (date ? date : "?") + "\n" + (commit ? commit : "?");
  showModal(t.c_str(), COL_BG);
}

void LvglUI::showConnecting(const char* hint, const char* ssid) {
  String t = stripAccents(hint) + "\n" + (ssid ? ssid : "?");
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
                        const char* tempUnit) {
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

#endif // WAVESHARE_ESP32C6_LCD
