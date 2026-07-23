/**
 * ui_wifi.h — tela de WiFi (scan da lista + teclado de senha)
 *
 * Extraido do claude_stick.ino na quebra do monolito (ZYN-379).
 */
#ifndef UI_WIFI_H
#define UI_WIFI_H

#include "app_state.h"
#include "ui_helpers.h"

void wifi_populate();
void wifi_kb_cb(lv_event_t *e);
void wifi_rescan_cb(lv_event_t *e);
void ui_wifi();

#endif // UI_WIFI_H
