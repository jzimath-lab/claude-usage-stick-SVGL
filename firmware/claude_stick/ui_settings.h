/**
 * ui_settings.h — telas de Ajustes (lista rolavel) e Sobre
 *
 * Extraido do claude_stick.ino na quebra do monolito (ZYN-379).
 */
#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H

#include "app_state.h"
#include "ui_helpers.h"
#include "storage.h"

void settings_action_cb(lv_event_t *e);
void add_setting_row(lv_obj_t *p, const char *txt, int act, uint32_t fg, lv_obj_t **out);
void ui_settings();
void ui_about();

// Zera os ponteiros de UI proprios deste modulo. Chamado por render_state()
// antes de destruir a tela anterior — evita exportar os ponteiros um a um.
void ui_settings_invalidate();

#endif // UI_SETTINGS_H
