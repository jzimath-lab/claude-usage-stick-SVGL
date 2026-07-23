#include "ui_wifi.h"

void nav_cb(lv_event_t *e);   // navegacao generica, segue no claude_stick.ino

// ============================================================
// Tela: WiFi (scan + teclado)
// ============================================================
static lv_obj_t *wifi_list = nullptr, *wifi_ta = nullptr, *wifi_kb = nullptr, *wifi_status = nullptr;
static char sel_ssid[33] = {0};
static void wifi_item_cb(lv_event_t *e);

void wifi_populate() {
  lv_obj_clean(wifi_list);
  lv_label_set_text(wifi_status, TRS("Escaneando redes...", "Scanning networks..."));
  lv_refr_now(NULL);
  WiFiManager::NetworkInfo nets[12];
  int n = g_wifi.scanNetworks(nets, 12);
  for (int i = 0; i < n; i++) {
    lv_obj_t *b = lv_list_add_button(wifi_list, LV_SYMBOL_WIFI, nets[i].ssid);
    lv_obj_set_style_bg_color(b, lv_color_hex(C_SURFACE), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(C_TEXT), 0);
    lv_obj_add_event_cb(b, wifi_item_cb, LV_EVENT_CLICKED, NULL);  // clique direto no botão
  }
  lv_label_set_text(wifi_status, n > 0 ? TRS("Toque na sua rede", "Tap your network")
                                       : TRS("Nenhuma rede. Toque em Reescanear.", "No networks. Tap Rescan."));
}
static void wifi_item_cb(lv_event_t *e) {
  lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
  const char *txt = lv_list_get_button_text(wifi_list, btn);
  if (!txt) return;
  strlcpy(sel_ssid, txt, sizeof(sel_ssid));
  lv_label_set_text_fmt(wifi_status, TRS("Senha de \"%s\":", "Password for \"%s\":"), sel_ssid);
  lv_obj_add_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);
  lv_textarea_set_text(wifi_ta, "");
  lv_obj_clear_flag(wifi_ta, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(wifi_kb, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(wifi_kb, wifi_ta);
}
void wifi_kb_cb(lv_event_t *e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) {
    const char *pass = lv_textarea_get_text(wifi_ta);
    lv_label_set_text(wifi_status, TRS("Conectando...", "Connecting..."));
    lv_obj_add_flag(wifi_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_ta, LV_OBJ_FLAG_HIDDEN);
    lv_refr_now(NULL);
    bool ok = g_wifi.connectTo(sel_ssid, pass, 15000);
    if (ok) request_state(g_onboarding ? ST_TOKEN : ST_LOADING);
    else { lv_label_set_text(wifi_status, TRS("Falhou. Toque numa rede de novo.", "Failed. Tap a network again.")); lv_obj_clear_flag(wifi_list, LV_OBJ_FLAG_HIDDEN); }
  } else if (code == LV_EVENT_CANCEL) {
    lv_obj_add_flag(wifi_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(wifi_ta, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(wifi_list, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(wifi_status, TRS("Toque na sua rede", "Tap your network"));
  }
}
void wifi_rescan_cb(lv_event_t *e) { (void)e; wifi_populate(); }

void ui_wifi() {
  lv_obj_t *scr = lv_screen_active();
  lv_obj_t *title = mklabel(scr, TRS("Configurar WiFi", "Configure WiFi"), &lv_font_montserrat_20, C_TEXT);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 14, 12);

  lv_obj_t *rb = mkbtn(scr, TRS("Reescanear", "Rescan"), &lv_font_montserrat_14, C_SURFACE2, C_ACCENT);
  lv_obj_align(rb, LV_ALIGN_TOP_RIGHT, -12, 8);
  lv_obj_add_event_cb(rb, wifi_rescan_cb, LV_EVENT_CLICKED, NULL);

  if (!g_onboarding && g_hasToken) {
    lv_obj_t *bk = mkbtn(scr, TRS(LV_SYMBOL_LEFT " Voltar", LV_SYMBOL_LEFT " Back"),
                         &lv_font_montserrat_14, C_SURFACE2, C_MUTED);
    lv_obj_align(bk, LV_ALIGN_TOP_RIGHT, -150, 8);
    lv_obj_add_event_cb(bk, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(g_usage.ok ? ST_MAIN : ST_SETTINGS));
  }

  wifi_status = mklabel(scr, "...", &lv_font_montserrat_14, C_MUTED);
  lv_obj_align(wifi_status, LV_ALIGN_TOP_LEFT, 14, 44);

  wifi_list = lv_list_create(scr);
  lv_obj_set_size(wifi_list, 452, 246);
  lv_obj_align(wifi_list, LV_ALIGN_TOP_MID, 0, 68);
  lv_obj_set_style_bg_color(wifi_list, lv_color_hex(C_BG), 0);
  lv_obj_set_style_border_color(wifi_list, lv_color_hex(C_BORDER), 0);
  // clique é anexado por botão em wifi_populate()

  wifi_ta = lv_textarea_create(scr);
  lv_textarea_set_one_line(wifi_ta, true);
  lv_textarea_set_password_mode(wifi_ta, true);
  lv_textarea_set_placeholder_text(wifi_ta, TRS("senha do WiFi", "WiFi password"));
  lv_obj_set_size(wifi_ta, 452, 44);
  lv_obj_align(wifi_ta, LV_ALIGN_TOP_MID, 0, 66);
  lv_obj_add_flag(wifi_ta, LV_OBJ_FLAG_HIDDEN);

  wifi_kb = lv_keyboard_create(scr);
  lv_obj_add_flag(wifi_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(wifi_kb, wifi_kb_cb, LV_EVENT_ALL, NULL);

  wifi_populate();
}

