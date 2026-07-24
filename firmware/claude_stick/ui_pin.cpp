#include "ui_pin.h"
#include "storage.h"

// ============================================================
// Tela: PIN (keypad touch) — entra PIN p/ decifrar OU define novo no setup
// ============================================================
static const char *pin_map[] = {
  "1", "2", "3", "\n",
  "4", "5", "6", "\n",
  "7", "8", "9", "\n",
  LV_SYMBOL_LEFT, "0", LV_SYMBOL_OK, ""
};

void pin_update_dots() {
  if (!g_pinDots) return;
  char dots[24] = {0};
  int len = strlen(g_pinEntry);
  for (int i = 0; i < PIN_LEN; i++) {
    strcat(dots, i < len ? "*" : "_");
    if (i < PIN_LEN - 1) strcat(dots, " ");
  }
  lv_label_set_text(g_pinDots, dots);
}

void pin_submit() {
  if (g_state == ST_SETUP_PIN) {
    if (!g_pinConfirming) {
      strlcpy(g_pinFirst, g_pinEntry, sizeof(g_pinFirst));
      g_pinConfirming = true;
      g_pinEntry[0] = 0;
      pin_update_dots();
      if (g_pinMsg) lv_label_set_text(g_pinMsg, TRS("Confirme o PIN", "Confirm the PIN"));
      return;
    }
    // confirmando
    if (strcmp(g_pinFirst, g_pinEntry) != 0) {
      g_pinConfirming = false;
      g_pinFirst[0] = 0; g_pinEntry[0] = 0;
      pin_update_dots();
      if (g_pinMsg) lv_label_set_text(g_pinMsg, TRS("Nao bateu. Defina de novo.", "Didn't match. Set it again."));
      return;
    }
    // PIN definido -> cifra o token pendente e salva
    if (!encryptToken(g_pendingToken, g_pinEntry, g_blob)) {
      if (g_pinMsg) lv_label_set_text(g_pinMsg, TRS("Falha ao cifrar. Tente de novo.", "Encryption failed. Try again."));
      g_pinConfirming = false; g_pinFirst[0] = 0; g_pinEntry[0] = 0; pin_update_dots();
      return;
    }
    save_blob();
    strlcpy(g_token, g_pendingToken, sizeof(g_token));
    memset(g_pendingToken, 0, sizeof(g_pendingToken));
    g_hasToken = true; g_onboarding = false;
    g_pinAttempts = 0; save_attempts();
    g_pinConfirming = false; g_pinFirst[0] = 0; g_pinEntry[0] = 0;
    Serial.println("[PIN] token cifrado e salvo");
    request_state(g_wifi.isConnected() ? ST_LOADING : ST_WIFI);
    return;
  }

  // ST_PIN: tenta decifrar
  if (decryptToken(g_blob, g_pinEntry, g_token, sizeof(g_token))) {
    g_pinAttempts = 0; save_attempts();
    g_pinEntry[0] = 0;
    Serial.printf("[PIN] ok, token %d chars\n", (int)strlen(g_token));
    if (!g_wifi.isConnected()) g_wifi.autoConnect(WIFI_CONNECT_TIMEOUT_MS);
    request_state(g_wifi.isConnected() ? ST_LOADING : ST_WIFI);
  } else {
    g_pinAttempts++; save_attempts();
    g_pinEntry[0] = 0; pin_update_dots();
    if (g_pinAttempts >= MAX_PIN_ATTEMPTS) {
      Serial.println("[PIN] limite estourado -> wipe");
      factory_reset();
      request_state(ST_WIFI);
      return;
    }
    int wait = LOCKOUT_BASE_SEC * (1 << (g_pinAttempts - 1));
    if (wait > 3600) wait = 3600;
    g_lockoutUntil = millis() + (uint32_t)wait * 1000;
    if (g_pinMsg) {
      char m[64];
      snprintf(m, sizeof(m), TRS("PIN errado (%d/%d). Aguarde %ds", "Wrong PIN (%d/%d). Wait %ds"),
               g_pinAttempts, MAX_PIN_ATTEMPTS, wait);
      lv_label_set_text(g_pinMsg, m);
    }
  }
}

void pin_kb_cb(lv_event_t *e) {
  if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
  if (millis() < g_lockoutUntil) return;     // travado
  lv_obj_t *bm = (lv_obj_t *)lv_event_get_target(e);
  uint32_t id = lv_buttonmatrix_get_selected_button(bm);
  const char *txt = lv_buttonmatrix_get_button_text(bm, id);
  if (!txt) return;
  int len = strlen(g_pinEntry);
  if (strcmp(txt, LV_SYMBOL_LEFT) == 0) {
    if (len > 0) g_pinEntry[len - 1] = 0;
    pin_update_dots();
  } else if (strcmp(txt, LV_SYMBOL_OK) == 0) {
    if (len == PIN_LEN) pin_submit();
  } else if (len < PIN_LEN) {
    g_pinEntry[len] = txt[0];
    g_pinEntry[len + 1] = 0;
    pin_update_dots();
    if (len + 1 == PIN_LEN) pin_submit();     // auto-submit ao completar
  }
}

void ui_pin() {
  lv_obj_t *scr = lv_screen_active();
  const char *title = (g_state == ST_SETUP_PIN)
    ? (g_pinConfirming ? TRS("Confirme o PIN", "Confirm the PIN") : TRS("Defina um PIN", "Set a PIN"))
    : TRS("Digite o PIN", "Enter the PIN");
  lv_obj_t *t = mklabel(scr, title, &lv_font_montserrat_22, C_TEXT);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 14);

  g_pinDots = mklabel(scr, "", &lv_font_montserrat_28, C_ACCENT);
  lv_obj_align(g_pinDots, LV_ALIGN_TOP_MID, 0, 48);
  pin_update_dots();

  const char *sub = (g_state == ST_SETUP_PIN)
    ? TRS("Voce vai digita-lo a cada boot.", "You'll type it on every boot.")
    : TRS("Necessario para desbloquear o token.", "Needed to unlock the token.");
  g_pinMsg = mklabel(scr, sub, &lv_font_montserrat_14, C_MUTED);
  lv_obj_align(g_pinMsg, LV_ALIGN_TOP_MID, 0, 86);

  lv_obj_t *bm = lv_buttonmatrix_create(scr);
  lv_buttonmatrix_set_map(bm, pin_map);
  lv_obj_set_size(bm, 280, 180);
  lv_obj_align(bm, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_set_style_bg_color(bm, lv_color_hex(C_BG), 0);
  lv_obj_set_style_border_width(bm, 0, 0);
  lv_obj_set_style_text_font(bm, &lv_font_montserrat_24, 0);
  lv_obj_set_style_bg_color(bm, lv_color_hex(C_SURFACE2), LV_PART_ITEMS);
  lv_obj_set_style_text_color(bm, lv_color_hex(C_TEXT), LV_PART_ITEMS);
  lv_obj_add_event_cb(bm, pin_kb_cb, LV_EVENT_VALUE_CHANGED, NULL);

  if (millis() < g_lockoutUntil && g_pinMsg) {
    int rem = (g_lockoutUntil - millis()) / 1000;
    char m[48]; snprintf(m, sizeof(m), TRS("Aguarde %ds", "Wait %ds"), rem);
    lv_label_set_text(g_pinMsg, m);
  }
}



// Zera os ponteiros de UI proprios deste modulo (ver render_state).
void ui_pin_invalidate() { g_pinDots = g_pinMsg = nullptr; }
