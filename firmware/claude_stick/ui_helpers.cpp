#include "ui_helpers.h"
#include "cota_sev.h"

lv_obj_t *mklabel(lv_obj_t *p, const char *txt, const lv_font_t *font, uint32_t color) {
  lv_obj_t *l = lv_label_create(p);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}
void no_box(lv_obj_t *o) {
  lv_obj_set_style_bg_opa(o, 0, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}
// Botão pílula com label centralizado; user_data leva o State alvo (nav_cb).
lv_obj_t *mkbtn(lv_obj_t *p, const char *txt, const lv_font_t *font,
                       uint32_t bg, uint32_t fg) {
  lv_obj_t *b = lv_button_create(p);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_center(mklabel(b, txt, font, fg));
  return b;
}
uint32_t pct_color(float p) {
  if (p < 70.0f) return C_OK;
  if (p < 90.0f) return C_WARN;
  return C_BAD;
}
// gradiente contínuo verde -> âmbar -> vermelho conforme o uso cresce
// Cor por FAIXA, e nao mais gradiente continuo — §7 do S4, aprovado em 25/08.
//
// ⚠️ POR QUE O GRADIENTE SAIU. Num continuo, 74% e 75% sao visualmente
// identicos: a cor muda um pouco a cada ponto, entao NENHUMA transicao
// significa algo. O olho nao le "cruzou o limite" porque nao ha limite. E ele
// pintava 60% de laranja-avermelhado, alarmando cedo — alarme constante vira
// ruido e o custo recai nas outras leituras da tela.
//
// Tres estados discretos fazem a mudanca de cor virar EVENTO, que e o que se
// percebe de relance num painel de mesa.
//
// A funcao pura vive em cota_sev.h, compartilhada com o teste de host —
// os limiares sao verificados nas BORDAS (74/75, 99/100), que e onde um `>` no
// lugar de `>=` se esconde.
lv_color_t grad_color(float p) {
  return lv_color_hex(cota_sev_cor(p));
}
// acende os segmentos do medidor; acesos ganham a cor do gradiente,
// apagados ficam no trilho escuro
void set_meter(lv_obj_t **seg, float pct) {
  int filled = (int)(pct / 100.0f * NSEG + 0.5f);
  if (pct > 0.5f && filled == 0) filled = 1;
  if (filled > NSEG) filled = NSEG;
  lv_color_t col = grad_color(pct);
  for (int i = 0; i < NSEG; i++) {
    if (!seg[i]) continue;
    lv_obj_set_style_bg_color(seg[i], (i < filled) ? col : lv_color_hex(C_TRACK), 0);
    lv_obj_set_style_bg_opa(seg[i], (i < filled) ? LV_OPA_COVER : 160, 0);
  }
}
// "reseta em 1h 23m" / "2d 4h" / "agora" / "--" (relógio não sincronizado)
void fmt_eta(uint32_t epoch, char *out, int sz) {
  time_t now = time(nullptr);
  if (now < 1000000000L || epoch == 0) { snprintf(out, sz, "--"); return; }
  long d = (long)epoch - (long)now;
  if (d <= 0) { snprintf(out, sz, "%s", TRS("agora", "now")); return; }
  int days = d / 86400; d %= 86400;
  int hrs  = d / 3600;  d %= 3600;
  int mins = d / 60;
  if (days > 0)      snprintf(out, sz, "%dd %dh", days, hrs);
  else if (hrs > 0)  snprintf(out, sz, "%dh %02dm", hrs, mins);
  else               snprintf(out, sz, "%dm", mins);
}
void fmt_clock(uint32_t epoch, char *out, int sz) {
  if (epoch == 0 || time(nullptr) < 1000000000L) { strlcpy(out, "--:--", sz); return; }
  time_t t = (time_t)epoch; struct tm tmv;
  localtime_r(&t, &tmv);
  strftime(out, sz, "%a %H:%M", &tmv);
}
void fmt_hm(uint32_t epoch, char *out, int sz) {
  if (epoch == 0 || time(nullptr) < 1000000000L) { strlcpy(out, "--:--", sz); return; }
  time_t t = (time_t)epoch; struct tm tmv;
  localtime_r(&t, &tmv);
  strftime(out, sz, "%H:%M", &tmv);
}
// 1234 -> "1.2k", 2345678 -> "2.3M"
void fmt_tok(long long v, char *out, int sz) {
  if (v >= 100000000LL)     snprintf(out, sz, "%lldM", v / 1000000LL);
  else if (v >= 1000000LL)  snprintf(out, sz, "%.1fM", v / 1e6);
  else if (v >= 10000LL)    snprintf(out, sz, "%lldk", v / 1000LL);
  else if (v >= 1000LL)     snprintf(out, sz, "%.1fk", v / 1e3);
  else                      snprintf(out, sz, "%lld", v);
}
