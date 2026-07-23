/**
 * display.h — pipeline de display e touch do LVGL.
 *
 * Pipeline validado no bring-up (ver firmware/REFERENCIA-HARDWARE-LVGL.md):
 * LVGL desenha em 480x320 -> rotaciona 270 CW -> Arduino_Canvas 320x480 -> QSPI.
 * Nao alterar sem revalidar no hardware.
 */
#ifndef DISPLAY_H
#define DISPLAY_H

#include "app_state.h"

void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);
void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data);

#endif // DISPLAY_H
