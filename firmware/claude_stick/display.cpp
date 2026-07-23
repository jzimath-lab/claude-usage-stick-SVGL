#include "display.h"

// LVGL entrega 480x320 (paisagem); o painel e 320x480 fisico. A rotacao de 270
// CW e feita aqui, na copia para o canvas, e nao pelo driver.
void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
  uint16_t *src = (uint16_t *)px_map;
  for (int ly = 0; ly < SCREEN_HEIGHT; ly++) {
    uint16_t *src_row = src + ly * SCREEN_WIDTH;
    for (int lx = 0; lx < SCREEN_WIDTH; lx++)
      canvas_fb[(479 - lx) * 320 + ly] = src_row[lx];
  }
  gfx->flush();
  lv_disp_flush_ready(disp);
}

void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
  uint16_t x, y;
  if (touch_dev.touched()) {
    touch_dev.readData(&x, &y);
    data->point.x = x; data->point.y = y;
    data->state = LV_INDEV_STATE_PRESSED;
    g_lastTouchMs = millis();          // pausa o slideshow enquanto ha interacao
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}
