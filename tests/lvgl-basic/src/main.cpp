// Minimal LVGL test for Waveshare ESP32-C6 Touch LCD 1.47.
// Goal: prove LVGL stack compiles and renders on this board with the
// correct pins / JD9853 init — no touch, no demos yet.

#include <lvgl.h>
#include <Arduino_GFX_Library.h>

// Pin defs verified against Waveshare supplier 01_gfx_helloworld example.
#define GFX_BL 23

Arduino_DataBus *bus = new Arduino_HWSPI(15 /* DC */, 14 /* CS */, 1 /* SCK */, 2 /* MOSI */);
Arduino_GFX *gfx = new Arduino_ST7789(
    bus, 22 /* RST */, 0 /* rotation */, false /* IPS */,
    172, 320, 34, 0, 34, 0);

// JD9853 register init — required before the backlight LED can turn on.
void lcd_reg_init(void) {
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
  bus->batchOperation(init_operations, sizeof(init_operations));
}

// LVGL display driver glue
static lv_disp_draw_buf_t draw_buf;
static lv_color_t* disp_draw_buf;
static lv_disp_drv_t  disp_drv;

static void my_disp_flush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
  uint32_t w = area->x2 - area->x1 + 1;
  uint32_t h = area->y2 - area->y1 + 1;
#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t*)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)&color_p->full, w, h);
#endif
  lv_disp_flush_ready(drv);
}

static void build_ui() {
  lv_obj_t* scr = lv_scr_act();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), LV_PART_MAIN);

  // Title
  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, "LVGL OK");
  lv_obj_set_style_text_color(title, lv_color_hex(0x4FC3F7), 0);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

  // Subtitle with Swedish chars to verify font has them
  lv_obj_t* sub = lv_label_create(scr);
  lv_label_set_text(sub, "Vastra Lassby - ÅÄÖ");
  lv_obj_set_style_text_color(sub, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 32);

  // Animated bar
  lv_obj_t* bar = lv_bar_create(scr);
  lv_obj_set_size(bar, 280, 18);
  lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x263238), LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x4CAF50), LV_PART_INDICATOR);

  // Animate the bar from 0 to 100 over 3s, loop
  static lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, bar);
  lv_anim_set_values(&a, 0, 100);
  lv_anim_set_time(&a, 3000);
  lv_anim_set_playback_time(&a, 3000);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
    lv_bar_set_value((lv_obj_t*)obj, v, LV_ANIM_OFF);
  });
  lv_anim_start(&a);

  // Footer
  lv_obj_t* foot = lv_label_create(scr);
  lv_label_set_text(foot, "172x320 ST7789/JD9853");
  lv_obj_set_style_text_color(foot, lv_color_hex(0x90A4AE), 0);
  lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -8);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("=== LVGL test boot ===");

  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }
  lcd_reg_init();
  gfx->setRotation(1);                  // landscape, same as main firmware
  gfx->fillScreen(RGB565_BLACK);

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);

  lv_init();

  uint32_t w = gfx->width();
  uint32_t h = gfx->height();
  uint32_t bufSize = w * 40;            // 40-line partial buffer

  disp_draw_buf = (lv_color_t*)heap_caps_malloc(
      bufSize * sizeof(lv_color_t),
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!disp_draw_buf) {
    disp_draw_buf = (lv_color_t*)heap_caps_malloc(
        bufSize * sizeof(lv_color_t), MALLOC_CAP_8BIT);
  }
  if (!disp_draw_buf) {
    Serial.println("FATAL: draw_buf alloc failed");
    return;
  }

  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, bufSize);
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res  = w;
  disp_drv.ver_res  = h;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  build_ui();
  Serial.println("Setup done");
}

void loop() {
  lv_timer_handler();
  delay(5);
}
