#include "ui_message.h"

// Ainda no claude_stick.ino; migra para web_server.* (secao de onboarding).
lv_obj_t *build_claude_mark(lv_obj_t *parent);

// ============================================================
// Tela: loading / mensagem
// ============================================================
void ui_message(const char *title, const char *sub, uint32_t color) {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_t *t = mklabel(scr, title, &lv_font_montserrat_24, color);
  lv_obj_align(t, LV_ALIGN_CENTER, 0, -16);
  if (sub && sub[0]) {
    lv_obj_t *s = mklabel(scr, sub, &lv_font_montserrat_16, C_MUTED);
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 20);
  }
}
void ui_loading(const char *sub) {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_t *mark = build_claude_mark(scr);
  lv_obj_align(mark, LV_ALIGN_CENTER, 0, -50);
  lv_obj_t *t = mklabel(scr, TRS("Carregando seu uso...", "Loading your usage..."), &lv_font_montserrat_18, C_TEXT);
  lv_obj_align(t, LV_ALIGN_CENTER, 0, 28);
  if (sub && sub[0]) {
    lv_obj_t *s = mklabel(scr, sub, &lv_font_montserrat_12, C_MUTED);
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 52);
  }
  lv_obj_t *spn = lv_spinner_create(scr);
  lv_spinner_set_anim_params(spn, 1200, 70);
  lv_obj_set_size(spn, 34, 34);
  lv_obj_align(spn, LV_ALIGN_CENTER, 0, 90);
  lv_obj_set_style_arc_color(spn, lv_color_hex(C_SURFACE2), LV_PART_MAIN);
  lv_obj_set_style_arc_color(spn, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(spn, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_width(spn, 4, LV_PART_INDICATOR);
}
