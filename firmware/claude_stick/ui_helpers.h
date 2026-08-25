/**
 * ui_helpers.h — construtores de widget, cores por percentual e formatadores.
 *
 * Sem estado proprio: tudo aqui e funcao pura ou cria objeto LVGL a partir dos
 * parametros. Pode ser incluido por qualquer modulo de tela.
 */
#ifndef UI_HELPERS_H
#define UI_HELPERS_H

#include "app_state.h"

// ---- Construtores de widget ----
lv_obj_t *mklabel(lv_obj_t *p, const char *txt, const lv_font_t *font, uint32_t color);
void      no_box(lv_obj_t *o);
// Botao pilula com label centralizado; user_data leva o State alvo (nav_cb).
lv_obj_t *mkbtn(lv_obj_t *p, const char *txt, const lv_font_t *font,
                uint32_t bg, uint32_t fg);

// ---- Cor por percentual ----
uint32_t   pct_color(float p);    // faixas discretas: ok / warn / bad
// Cor por FAIXA (§7): ate 74 verde · 75-99 ambar · 100+ vermelho.
// O nome ficou por compatibilidade com os seis pontos de chamada; nao e
// mais gradiente. Ver o comentario em ui_helpers.cpp.
lv_color_t grad_color(float p);
void       set_meter(lv_obj_t **seg, float pct);

// ---- Formatadores ----
void fmt_eta(uint32_t epoch, char *out, int sz);    // "1h 23m" / "2d 4h" / "agora" / "--"
void fmt_clock(uint32_t epoch, char *out, int sz);  // "Qua 16:40"
void fmt_hm(uint32_t epoch, char *out, int sz);     // "16:40"
void fmt_tok(long long v, char *out, int sz);       // 1234 -> "1.2k", 2345678 -> "2.3M"

#endif // UI_HELPERS_H
