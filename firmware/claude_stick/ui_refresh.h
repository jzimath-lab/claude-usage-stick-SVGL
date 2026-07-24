/**
 * ui_refresh.h — atualizacao de valores do dashboard (tokens, tick, tendencia, heatmap, header)
 *
 * Extraido do claude_stick.ino na quebra do monolito (ZYN-379).
 */
#ifndef UI_REFRESH_H
#define UI_REFRESH_H

#include "app_state.h"
#include "ui_helpers.h"

void update_tok_row();
void trend_redraw();
void heat_redraw();

void dash_tick();
void refresh_ui_values();
void set_hdr_status();

void refresh_codex_values();

#endif // UI_REFRESH_H
