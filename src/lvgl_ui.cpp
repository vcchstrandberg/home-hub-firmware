// LVGL UI implementation for Waveshare ESP32-C6 Touch LCD 1.47.
// Compiled only when WAVESHARE_ESP32C6_LCD is defined; a no-op otherwise.

#ifdef WAVESHARE_ESP32C6_LCD

#include "lvgl_ui.h"
#include <lvgl.h>
#include <Arduino_GFX_Library.h>
#include <esp_heap_caps.h>

// ─── Pin defs (Touch variant — see project_waveshare_touch_lcd_pinout memory) ──
#define GFX_BL 23

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
  lv_obj_t* header;
  lv_obj_t* city_label;
  lv_obj_t* locale_label;

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

static void buildUI() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), LV_PART_MAIN);
  lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  // Header
  ui.header = lv_obj_create(scr);
  lv_obj_set_size(ui.header, 320, 24);
  lv_obj_set_pos(ui.header, 0, 0);
  styleContainer(ui.header, COL_HEADER_BG, 0, 0, 0, 0);

  ui.city_label = lv_label_create(ui.header);
  lv_label_set_text(ui.city_label, "-");
  lv_obj_set_style_text_color(ui.city_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.city_label, &lv_font_montserrat_14, 0);
  lv_obj_align(ui.city_label, LV_ALIGN_LEFT_MID, 6, 0);

  ui.locale_label = lv_label_create(ui.header);
  lv_label_set_text(ui.locale_label, "--");
  lv_obj_set_style_text_color(ui.locale_label, lv_color_hex(COL_DETAIL), 0);
  lv_obj_set_style_text_font(ui.locale_label, &lv_font_montserrat_12, 0);
  lv_obj_align(ui.locale_label, LV_ALIGN_RIGHT_MID, -6, 0);

  // Indoor card (amber accent)
  ui.indoor_card = lv_obj_create(scr);
  lv_obj_set_size(ui.indoor_card, 156, 102);
  lv_obj_set_pos(ui.indoor_card, 2, 28);
  styleContainer(ui.indoor_card, COL_CARD_BG, COL_INDOOR_ACC, 1, 5, 6);

  ui.indoor_name = lv_label_create(ui.indoor_card);
  lv_label_set_text(ui.indoor_name, "INNE");
  lv_obj_set_style_text_color(ui.indoor_name, lv_color_hex(COL_INDOOR_ACC), 0);
  lv_obj_set_style_text_font(ui.indoor_name, &lv_font_montserrat_14, 0);
  lv_obj_align(ui.indoor_name, LV_ALIGN_TOP_LEFT, 0, 0);

  ui.indoor_temp = lv_label_create(ui.indoor_card);
  lv_label_set_text(ui.indoor_temp, "--");
  lv_obj_set_style_text_color(ui.indoor_temp, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.indoor_temp, &lv_font_montserrat_36, 0);
  lv_obj_align(ui.indoor_temp, LV_ALIGN_LEFT_MID, 0, 4);

  ui.indoor_unit = lv_label_create(ui.indoor_card);
  lv_label_set_text(ui.indoor_unit, "C");
  lv_obj_set_style_text_color(ui.indoor_unit, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_style_text_font(ui.indoor_unit, &lv_font_montserrat_24, 0);
  lv_obj_align_to(ui.indoor_unit, ui.indoor_temp, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -4);

  ui.indoor_humidity = lv_label_create(ui.indoor_card);
  lv_label_set_text(ui.indoor_humidity, "Fukt: --");
  lv_obj_set_style_text_color(ui.indoor_humidity, lv_color_hex(COL_DETAIL), 0);
  lv_obj_set_style_text_font(ui.indoor_humidity, &lv_font_montserrat_12, 0);
  lv_obj_align(ui.indoor_humidity, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  // Outdoor card (blue accent)
  ui.outdoor_card = lv_obj_create(scr);
  lv_obj_set_size(ui.outdoor_card, 156, 102);
  lv_obj_set_pos(ui.outdoor_card, 162, 28);
  styleContainer(ui.outdoor_card, COL_CARD_BG, COL_OUTDOOR_ACC, 1, 5, 6);

  ui.outdoor_name = lv_label_create(ui.outdoor_card);
  lv_label_set_text(ui.outdoor_name, "UTE");
  lv_obj_set_style_text_color(ui.outdoor_name, lv_color_hex(COL_OUTDOOR_ACC), 0);
  lv_obj_set_style_text_font(ui.outdoor_name, &lv_font_montserrat_14, 0);
  lv_obj_align(ui.outdoor_name, LV_ALIGN_TOP_LEFT, 0, 0);

  ui.outdoor_temp = lv_label_create(ui.outdoor_card);
  lv_label_set_text(ui.outdoor_temp, "--");
  lv_obj_set_style_text_color(ui.outdoor_temp, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.outdoor_temp, &lv_font_montserrat_36, 0);
  lv_obj_align(ui.outdoor_temp, LV_ALIGN_LEFT_MID, 0, 4);

  ui.outdoor_unit = lv_label_create(ui.outdoor_card);
  lv_label_set_text(ui.outdoor_unit, "C");
  lv_obj_set_style_text_color(ui.outdoor_unit, lv_color_hex(COL_MUTED), 0);
  lv_obj_set_style_text_font(ui.outdoor_unit, &lv_font_montserrat_24, 0);
  lv_obj_align_to(ui.outdoor_unit, ui.outdoor_temp, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -4);

  ui.outdoor_pressure = lv_label_create(ui.outdoor_card);
  lv_label_set_text(ui.outdoor_pressure, "Tryck: --");
  lv_obj_set_style_text_color(ui.outdoor_pressure, lv_color_hex(COL_DETAIL), 0);
  lv_obj_set_style_text_font(ui.outdoor_pressure, &lv_font_montserrat_12, 0);
  lv_obj_align(ui.outdoor_pressure, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  // Rain row at the bottom
  ui.rain_row = lv_obj_create(scr);
  lv_obj_set_size(ui.rain_row, 320, 38);
  lv_obj_set_pos(ui.rain_row, 0, 134);
  styleContainer(ui.rain_row, COL_RAIN_BG, 0, 0, 0, 4);

  ui.rain_label = lv_label_create(ui.rain_row);
  lv_label_set_text(ui.rain_label, "REGN");
  lv_obj_set_style_text_color(ui.rain_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.rain_label, &lv_font_montserrat_14, 0);
  lv_obj_align(ui.rain_label, LV_ALIGN_LEFT_MID, 4, 0);

  ui.rain_droplet = lv_obj_create(ui.rain_row);
  lv_obj_set_size(ui.rain_droplet, 10, 10);
  styleContainer(ui.rain_droplet, 0xFFFFFF, 0, 0, 5, 0);
  lv_obj_align(ui.rain_droplet, LV_ALIGN_LEFT_MID, 56, 0);
  lv_obj_add_flag(ui.rain_droplet, LV_OBJ_FLAG_HIDDEN);

  ui.rain_1h = lv_label_create(ui.rain_row);
  lv_label_set_text(ui.rain_1h, "1h: --");
  lv_obj_set_style_text_color(ui.rain_1h, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.rain_1h, &lv_font_montserrat_14, 0);
  lv_obj_align(ui.rain_1h, LV_ALIGN_LEFT_MID, 76, 0);

  ui.rain_24h = lv_label_create(ui.rain_row);
  lv_label_set_text(ui.rain_24h, "24h: --");
  lv_obj_set_style_text_color(ui.rain_24h, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.rain_24h, &lv_font_montserrat_14, 0);
  lv_obj_align(ui.rain_24h, LV_ALIGN_RIGHT_MID, -4, 0);

  // Modal overlay (full-screen, hidden by default)
  ui.modal = lv_obj_create(scr);
  lv_obj_set_size(ui.modal, 320, 172);
  lv_obj_set_pos(ui.modal, 0, 0);
  styleContainer(ui.modal, COL_BG, 0, 0, 0, 12);

  ui.modal_label = lv_label_create(ui.modal);
  lv_label_set_text(ui.modal_label, "");
  lv_obj_set_style_text_color(ui.modal_label, lv_color_white(), 0);
  lv_obj_set_style_text_font(ui.modal_label, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_align(ui.modal_label, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(ui.modal_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(ui.modal_label, 296);
  lv_obj_center(ui.modal_label);

  lv_obj_add_flag(ui.modal, LV_OBJ_FLAG_HIDDEN);
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

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

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

  buildUI();
}

void LvglUI::tick() {
  lv_timer_handler();
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

void LvglUI::setHeader(const char* city, const char* localeCode) {
  hideModal();
  lv_label_set_text(ui.city_label,   stripAccents(city && *city ? city : "-").c_str());
  lv_label_set_text(ui.locale_label, localeCode ? localeCode : "--");
}

void LvglUI::setIndoor(const char* label, const char* humidityLabel,
                       float tempDisp, int humidity, const char* tempUnit) {
  hideModal();
  lv_label_set_text(ui.indoor_name, stripAccents(label).c_str());
  char buf[16];
  snprintf(buf, sizeof(buf), "%.1f", tempDisp);
  lv_label_set_text(ui.indoor_temp, buf);
  lv_label_set_text(ui.indoor_unit, tempUnit ? tempUnit : "");
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
