/**
 * ui_dashboard.h — helpers visuais do dashboard (cards, chips, mascotes, retangulos)
 *
 * Extraido do claude_stick.ino na quebra do monolito (ZYN-379).
 */
#ifndef UI_DASHBOARD_H
#define UI_DASHBOARD_H

#include "app_state.h"
#include "ui_helpers.h"

uint32_t status_color(const char *s);
const char *overall_label(const char *s);
lv_obj_t *tlabel(lv_obj_t *p, const lv_font_t *f, uint32_t c, int x, int y);
lv_obj_t *tstatic(lv_obj_t *p, const char *txt, const lv_font_t *f, uint32_t c, int x, int y);
void tile_setup(lv_obj_t *t);
lv_obj_t *card(lv_obj_t *p, int x, int y, int w, int h);
lv_obj_t *mkchip(lv_obj_t *p, int x, int y);
void set_chip(lv_obj_t *o, const char *txt, uint32_t col);
lv_obj_t *rrect(lv_obj_t *p, int x, int y, int w, int h, int r, uint32_t col);
int model_mood(int i);
void build_accessory(lv_obj_t *c, int model);
void build_model_mascot(lv_obj_t *parent, int cx, int i);
void model_chip(int i, char *out, size_t sz, uint32_t *col);
void build_win_card(lv_obj_t *t, int x, const char *title,
                    lv_obj_t **pct, lv_obj_t **seg, lv_obj_t **at, lv_obj_t **cd);

#endif // UI_DASHBOARD_H
