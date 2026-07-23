/**
 * ui_main.h — tela principal (header, tileview com 4 tiles, dots de navegacao)
 *
 * Extraido do claude_stick.ino na quebra do monolito (ZYN-379).
 */
#ifndef UI_MAIN_H
#define UI_MAIN_H

#include "app_state.h"
#include "ui_helpers.h"

void refresh_cb(lv_event_t *e);
void ui_main();

#endif // UI_MAIN_H
