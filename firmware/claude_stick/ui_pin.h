/**
 * ui_pin.h — tela de PIN (keypad touch): decifra o token ou define PIN novo no setup
 *
 * Extraido do claude_stick.ino na quebra do monolito (ZYN-379).
 */
#ifndef UI_PIN_H
#define UI_PIN_H

#include "app_state.h"
#include "ui_helpers.h"

void pin_update_dots();
void pin_submit();
void pin_kb_cb(lv_event_t *e);
void ui_pin();

void ui_pin_invalidate();

#endif // UI_PIN_H
