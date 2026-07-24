#include "ui_main.h"
#include "logo_assets.h"
#include "codex_logo.h"
#include "ui_moments.h"

// Ainda no claude_stick.ino; migram para ui_tiles.* e ui_refresh.*
void nav_cb(lv_event_t *e);
void start_data_web();
void tile_setup(lv_obj_t *t);
void build_tile_agora(lv_obj_t *t);
void build_tile_models(lv_obj_t *t);
void build_tile_trend(lv_obj_t *t);
void build_tile_heat(lv_obj_t *t);
void build_tile_codex(lv_obj_t *t);
void build_tile_codex_trend(lv_obj_t *t);
void build_tile_codex_heat(lv_obj_t *t);
void build_tile_codex_origem(lv_obj_t *t);
void build_tile_codex_modelo(lv_obj_t *t);
void build_tile_codex_inter(lv_obj_t *t);
void on_tile_changed(lv_event_t *e);
void dash_tick();
void refresh_ui_values();
void set_hdr_status();

void refresh_cb(lv_event_t *e) { (void)e; g_wantRefresh = true; }

void ui_main() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);

  start_data_web();

  // Header: Clawd + logotipo (duplo toque em QUALQUER um = demo das animacoes),
  // botao de ATUALIZAR visivel no centro, engrenagem grande a direita.
  lv_obj_t *hIcon = lv_image_create(scr);
  lv_image_set_src(hIcon, &img_clawd_sm);
  lv_obj_set_pos(hIcon, 14, 8);
  lv_obj_t *hWord = lv_image_create(scr);
  lv_image_set_src(hWord, &img_wordmark);
  lv_obj_set_pos(hWord, 66, 8);
  g_ui.hdrClawd = hIcon; g_ui.hdrWord = hWord;
  g_ui.hdrCodexIcon = lv_image_create(scr);
  lv_image_set_src(g_ui.hdrCodexIcon, &img_codex_sm);
  lv_obj_set_pos(g_ui.hdrCodexIcon, 14, 9);
  lv_obj_add_flag(g_ui.hdrCodexIcon, LV_OBJ_FLAG_HIDDEN);
  g_ui.hdrCodex = mklabel(scr, "CODEX", &lv_font_montserrat_20, C_CODEX);
  lv_obj_set_pos(g_ui.hdrCodex, 48, 12);
  lv_obj_add_flag(g_ui.hdrCodex, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *logoSpot = lv_obj_create(scr);     // hotspot icone+nome (so demo)
  lv_obj_set_pos(logoSpot, 6, 2); lv_obj_set_size(logoSpot, 128, 40);
  lv_obj_set_style_bg_opa(logoSpot, 0, 0);
  lv_obj_set_style_border_width(logoSpot, 0, 0);
  lv_obj_clear_flag(logoSpot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(logoSpot, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(logoSpot, [](lv_event_t *e) {
    (void)e;
    static uint32_t lastClick = 0;             // duplo clique manual (9.2 nao tem nativo)
    static int di = 0;
    uint32_t now = millis();
    if (now - lastClick < 450) {
      static const int T[4] = {25, 50, 70, 100};
      show_moment((di / 4) % 2, T[di % 4]);
      di++;
      lastClick = 0;
    } else {
      lastClick = now;
    }
  }, LV_EVENT_CLICKED, NULL);

  // botao de atualizar no centro do header (acao explicita; a busca e bloqueante)
  lv_obj_t *ref = mkbtn(scr, LV_SYMBOL_REFRESH, &lv_font_montserrat_20, C_SURFACE2, C_ACCENT);
  lv_obj_set_size(ref, 56, 40);
  lv_obj_set_ext_click_area(ref, 10);
  lv_obj_align(ref, LV_ALIGN_TOP_MID, 0, 2);
  lv_obj_add_event_cb(ref, refresh_cb, LV_EVENT_CLICKED, NULL);

  g_hdrStatus = mklabel(scr, "", &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(g_hdrStatus, LV_ALIGN_TOP_RIGHT, -92, 16);

  lv_obj_t *gear = mkbtn(scr, LV_SYMBOL_SETTINGS, &lv_font_montserrat_22, C_SURFACE2, C_TEXT);
  lv_obj_set_size(gear, 78, 40);
  lv_obj_set_ext_click_area(gear, 16);
  lv_obj_align(gear, LV_ALIGN_TOP_RIGHT, -6, 2);
  lv_obj_add_event_cb(gear, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)ST_SETTINGS);

  // Barra fina decrescente do próximo refresh (só indicador; o botão de
  // atualizar fica no centro do header — clique aqui causava refresh acidental)
  g_ui.refBar = lv_bar_create(scr);
  lv_obj_set_size(g_ui.refBar, 480, 3);
  lv_obj_set_pos(g_ui.refBar, 0, 40);
  lv_bar_set_range(g_ui.refBar, 0, 1000);
  lv_bar_set_value(g_ui.refBar, 1000, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(g_ui.refBar, lv_color_hex(C_SURFACE), LV_PART_MAIN);
  lv_obj_set_style_bg_color(g_ui.refBar, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_radius(g_ui.refBar, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(g_ui.refBar, 0, LV_PART_INDICATOR);
  lv_obj_clear_flag(g_ui.refBar, LV_OBJ_FLAG_CLICKABLE);

  // Telas (swipe horizontal)
  g_ui.tv = lv_tileview_create(scr);
  lv_obj_set_pos(g_ui.tv, 0, 46);
  lv_obj_set_size(g_ui.tv, 480, 250);
  lv_obj_set_style_bg_opa(g_ui.tv, 0, 0);
  lv_obj_set_style_border_width(g_ui.tv, 0, 0);
  lv_obj_set_scrollbar_mode(g_ui.tv, LV_SCROLLBAR_MODE_OFF);
  for (int i = 0; i < NTILE_ALL; i++) {
    g_ui.tile[i] = lv_tileview_add_tile(g_ui.tv, i, 0, LV_DIR_HOR);
    tile_setup(g_ui.tile[i]);
  }
  build_tile_agora(g_ui.tile[0]);
  build_tile_models(g_ui.tile[1]);
  build_tile_trend(g_ui.tile[2]);
  build_tile_heat(g_ui.tile[3]);
  // Deck Codex: dados ricos primeiro; Janela 7d (enche em dias) por último.
  // Ritmo por hora do Codex foi aposentado (pouco informativo p/ uso via servidor/bot).
  build_tile_codex(g_ui.tile[4]);          // Agora (5h + Semana)
  build_tile_codex_origem(g_ui.tile[5]);   // Origem do consumo
  build_tile_codex_modelo(g_ui.tile[6]);   // Modelo consumido
  build_tile_codex_inter(g_ui.tile[7]);    // Interações + gráfico por origem
  build_tile_codex_trend(g_ui.tile[8]);    // Janela 7 dias (histórico + projeção)
  lv_obj_add_event_cb(g_ui.tv, on_tile_changed, LV_EVENT_VALUE_CHANGED, NULL);

  // Dots (objetos; o ativo vira pílula)
  for (int i = 0; i < NTILE_ALL; i++) {
    g_ui.dots[i] = lv_obj_create(scr);
    lv_obj_set_size(g_ui.dots[i], 8, 8);
    lv_obj_set_style_radius(g_ui.dots[i], 4, 0);
    lv_obj_set_style_bg_color(g_ui.dots[i], lv_color_hex(C_BORDER), 0);
    lv_obj_set_style_border_width(g_ui.dots[i], 0, 0);
    lv_obj_clear_flag(g_ui.dots[i], LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(g_ui.dots[i], LV_ALIGN_BOTTOM_MID, (int)((i - (NTILE_ALL - 1) / 2.0f) * 18), -4);
  }

  refresh_ui_values();
  on_tile_changed(NULL);
}

#include "ui_settings.h"
// Navegação genérica
// ============================================================
