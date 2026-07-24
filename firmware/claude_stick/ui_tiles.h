/**
 * ui_tiles.h — builders dos 4 tiles (Agora, Modelos, Janela 5h, Ritmo)
 *
 * Extraido do claude_stick.ino na quebra do monolito (ZYN-379).
 */
#ifndef UI_TILES_H
#define UI_TILES_H

#include "app_state.h"
#include "ui_helpers.h"

void build_tile_agora(lv_obj_t *t);
void build_tile_models(lv_obj_t *t);
int tr_x(uint32_t tt, uint32_t ws, uint32_t we);
int tr_y(float p);
void build_tile_trend(lv_obj_t *t);
void heat_btn_style();
void heat_btn_cb(lv_event_t *e);
void build_tile_codex(lv_obj_t *t);
void build_tile_codex_trend(lv_obj_t *t);
void build_tile_codex_origem(lv_obj_t *t);
void build_tile_codex_modelo(lv_obj_t *t);
void build_tile_codex_inter(lv_obj_t *t);
void build_tile_heat(lv_obj_t *t);
void on_tile_changed(lv_event_t *e);

#endif // UI_TILES_H
