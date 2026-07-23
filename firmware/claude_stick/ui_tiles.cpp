#include "ui_tiles.h"
#include "ui_dashboard.h"
#include "ui_refresh.h"
#include "logo_assets.h"

// Helpers visuais ainda no claude_stick.ino; migram para ui_dashboard.*
void build_win_card(lv_obj_t *t, int x, const char *title,
                           lv_obj_t **pct, lv_obj_t **seg, lv_obj_t **at, lv_obj_t **cd);

void build_tile_agora(lv_obj_t *t) {
  build_win_card(t, 8,   TRS("5 HORAS", "5 HOURS"), &g_ui.agPct5, g_ui.seg5, &g_ui.agAt5, &g_ui.agCd5);
  build_win_card(t, 244, TRS("SEMANA", "WEEK"),     &g_ui.agPct7, g_ui.seg7, &g_ui.agAt7, &g_ui.agCd7);
  g_ui.agChip = mkchip(t, 8, 220);
  g_ui.agTok = tlabel(t, &lv_font_montserrat_12, C_MUTED, 130, 226);
  lv_obj_set_width(g_ui.agTok, 342);
  lv_obj_set_style_text_align(g_ui.agTok, LV_TEXT_ALIGN_RIGHT, 0);
}
// Tile 1 — MODELOS: Clawd oficial por modelo (humor animado) + sonda + incidentes.
void build_tile_models(lv_obj_t *t) {
  static const int CENTERS[NMODELS] = {60, 180, 300, 420};
  for (int i = 0; i < NMODELS; i++) {
    build_model_mascot(t, CENTERS[i], i);
    lv_obj_t *n = mklabel(t, g_models[i].name, &lv_font_montserrat_16,
                          model_mood(i) == 1 ? C_TEXT : C_MUTED);
    lv_obj_set_width(n, 104);
    lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(n, CENTERS[i] - 52, 98);
    g_ui.mChip[i] = mkchip(t, 0, 122);
  }
  tstatic(t, TRS("sonda real na API \xE2\x80\xA2 1 modelo por ciclo",
                 "live API probe \xE2\x80\xA2 1 model per cycle"),
          &lv_font_montserrat_12, C_FAINT, 14, 170);
  g_ui.incident = tlabel(t, &lv_font_montserrat_14, C_MUTED, 14, 194);
  lv_obj_set_width(g_ui.incident, 452);
  lv_label_set_long_mode(g_ui.incident, LV_LABEL_LONG_WRAP);
}
// Tile 2 — JANELA 5H: histórico + projeção pontilhada até esgotar.
#define TR_X0 12
#define TR_Y0 10
#define TR_W  440
#define TR_H  126
int tr_x(uint32_t tt, uint32_t ws, uint32_t we) {
  if (we <= ws) return TR_X0;
  long long v = (long long)(tt - ws) * TR_W / (long long)(we - ws);
  if (v < 0) v = 0; if (v > TR_W) v = TR_W;
  return TR_X0 + (int)v;
}
int tr_y(float p) {
  if (p < 0) p = 0; if (p > 100) p = 100;
  return TR_Y0 + TR_H - (int)(p * TR_H / 100.0f);
}
void build_tile_trend(lv_obj_t *t) {
  tstatic(t, TRS("Janela de 5h", "5-hour window"), &lv_font_montserrat_16, C_TEXT, 14, 2);
  tstatic(t, TRS("uso real + projecao", "real usage + projection"), &lv_font_montserrat_12, C_FAINT, 320, 6);

  lv_obj_t *c = card(t, 8, 26, 464, 170);
  lv_obj_set_style_pad_all(c, 0, 0);

  // grade: 25/50/75%
  for (int i = 1; i <= 3; i++) {
    lv_obj_t *g = rrect(c, TR_X0, tr_y(i * 25.0f), TR_W, 1, 0, C_GRID);
    (void)g;
  }
  tstatic(c, "100", &lv_font_montserrat_12, C_FAINT, TR_X0 + TR_W - 24, TR_Y0 - 6);
  tstatic(c, "0",   &lv_font_montserrat_12, C_FAINT, TR_X0 + TR_W - 10, TR_Y0 + TR_H - 14);

  // histórico (linha sólida coral)
  g_ui.trHist = lv_line_create(c);
  lv_obj_set_pos(g_ui.trHist, 0, 0);
  lv_obj_set_style_line_width(g_ui.trHist, 3, 0);
  lv_obj_set_style_line_color(g_ui.trHist, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_line_rounded(g_ui.trHist, true, 0);

  // projeção (pontilhada)
  g_ui.trProj = lv_line_create(c);
  lv_obj_set_pos(g_ui.trProj, 0, 0);
  lv_obj_set_style_line_width(g_ui.trProj, 2, 0);
  lv_obj_set_style_line_color(g_ui.trProj, lv_color_hex(C_ACCENT), 0);
  lv_obj_set_style_line_opa(g_ui.trProj, 170, 0);
  lv_obj_set_style_line_dash_width(g_ui.trProj, 6, 0);
  lv_obj_set_style_line_dash_gap(g_ui.trProj, 6, 0);

  // marcador do ponto atual
  g_ui.trDot = rrect(c, 0, 0, 8, 8, 4, C_TEXT);
  lv_obj_add_flag(g_ui.trDot, LV_OBJ_FLAG_HIDDEN);

  // horários do eixo X (início da janela / reset)
  g_ui.trT0 = tlabel(c, &lv_font_montserrat_12, C_FAINT, TR_X0, TR_Y0 + TR_H + 8);
  g_ui.trT1 = tlabel(c, &lv_font_montserrat_12, C_FAINT, TR_X0 + TR_W - 40, TR_Y0 + TR_H + 8);

  g_ui.trCap = tlabel(t, &lv_font_montserrat_16, C_MUTED, 14, 210);
  lv_obj_set_width(g_ui.trCap, 452);
  lv_label_set_long_mode(g_ui.trCap, LV_LABEL_LONG_WRAP);
}
// Tile 3 — RITMO: heatmap por hora com filtro de período.
void heat_btn_style() {
  const char *names[4] = {TRS("Hoje", "Today"), "7d", "30d", TRS("Tudo", "All")};
  for (int i = 0; i < 4; i++) {
    if (!g_ui.heatBtn[i]) continue;
    bool on = (i == g_heatMode);
    lv_obj_set_style_bg_color(g_ui.heatBtn[i], lv_color_hex(on ? C_ACCENT : C_SURFACE2), 0);
    lv_obj_t *l = lv_obj_get_child(g_ui.heatBtn[i], 0);
    if (l) {
      lv_label_set_text(l, names[i]);
      lv_obj_set_style_text_color(l, lv_color_hex(on ? C_BG : C_MUTED), 0);
    }
  }
}
void heat_btn_cb(lv_event_t *e) {
  int m = (int)(intptr_t)lv_event_get_user_data(e);
  if (m == g_heatMode) return;
  g_heatMode = m;
  g_prefs.putInt("heatm", m);
  heat_btn_style();
  heat_redraw();
}
void build_tile_heat(lv_obj_t *t) {
  tstatic(t, TRS("Ritmo por hora", "Hourly rhythm"), &lv_font_montserrat_16, C_TEXT, 14, 6);
  for (int i = 0; i < 4; i++) {
    lv_obj_t *b = lv_button_create(t);
    lv_obj_set_size(b, 52, 30);
    lv_obj_set_pos(b, 246 + i * 56, 0);
    lv_obj_set_style_radius(b, 15, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_ext_click_area(b, 6);
    lv_obj_t *l = mklabel(b, "", &lv_font_montserrat_14, C_MUTED);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, heat_btn_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    g_ui.heatBtn[i] = b;
  }
  heat_btn_style();
  for (int h = 0; h < 24; h++) {
    lv_obj_t *bar = lv_obj_create(t);
    lv_obj_set_size(bar, 13, 4);
    lv_obj_set_pos(bar, 18 + h * 18, 176);
    lv_obj_set_style_radius(bar, 3, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_ACCENT), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    g_ui.heat[h] = bar;
  }
  int ticks[5] = {0, 6, 12, 18, 23};
  for (int i = 0; i < 5; i++) {
    int h = ticks[i]; char s[4]; snprintf(s, sizeof(s), "%dh", h);
    lv_obj_t *l = mklabel(t, s, &lv_font_montserrat_12, C_MUTED);
    lv_obj_set_pos(l, 14 + h * 18, 186);
  }
  tstatic(t, TRS("quota da janela 5h queimada em cada hora local",
                 "5h-window quota burned per local hour"), &lv_font_montserrat_12, C_FAINT, 14, 214);
}

void on_tile_changed(lv_event_t *e) {
  (void)e;
  if (!g_ui.tv) return;
  lv_obj_t *act = lv_tileview_get_tile_active(g_ui.tv);
  for (int i = 0; i < NTILES; i++) {
    if (!g_ui.dots[i]) continue;
    bool on = (g_ui.tile[i] == act);
    if (on) g_curTile = i;
    lv_obj_set_style_bg_color(g_ui.dots[i], lv_color_hex(on ? C_ACCENT : C_BORDER), 0);
    lv_obj_set_width(g_ui.dots[i], on ? 18 : 8);
  }
}
