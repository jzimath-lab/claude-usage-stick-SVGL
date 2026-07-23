/**
 * ui_moments.h — animacoes de limiar (25/50/70/100% nas janelas 5h e semanal)
 *
 * Extraido do claude_stick.ino na quebra do monolito (ZYN-379).
 */
#ifndef UI_MOMENTS_H
#define UI_MOMENTS_H

#include "app_state.h"
#include "ui_helpers.h"

void check_thresholds();
void moment_close();
void moment_close_cb(lv_event_t *e);
void show_moment(int win, int thr);
void moment_tick();

bool moment_active();               // ha overlay de limiar na tela?
void moment_pump(bool refreshing);  // mostra o pendente e anima o ativo

#endif // UI_MOMENTS_H
