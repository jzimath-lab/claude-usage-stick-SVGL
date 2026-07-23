#include "storage.h"

void load_persisted() {
  g_prefs.begin(NVS_NAMESPACE, false);
  size_t n = g_prefs.getBytesLength("blob");
  if (n == sizeof(EncryptedBlob)) {
    g_prefs.getBytes("blob", &g_blob, sizeof(EncryptedBlob));
    g_hasToken = true;
  }
  g_pinAttempts = g_prefs.getInt("pinatt", 0);
  g_briIdx = g_prefs.getInt("bri", 1);
  if (g_briIdx < 0 || g_briIdx > 2) g_briIdx = 1;
  g_pollSec = g_prefs.getInt("poll", DEFAULT_POLL_SEC);
  if (g_pollSec < MIN_POLL_SEC || g_pollSec > MAX_POLL_SEC) g_pollSec = DEFAULT_POLL_SEC;
  g_tzOffset = g_prefs.getInt("tz", -3);
  if (g_tzOffset < -12 || g_tzOffset > 14) g_tzOffset = -3;
  g_slideSec = g_prefs.getInt("slide", 0);
  if (g_slideSec != 0 && g_slideSec != 5 && g_slideSec != 10 &&
      g_slideSec != 15 && g_slideSec != 30) g_slideSec = 0;
  g_heatMode = g_prefs.getInt("heatm", 3);
  if (g_heatMode < 0 || g_heatMode > 3) g_heatMode = 3;
  g_lang = g_prefs.getInt("lang", 0) ? 1 : 0;
}
void save_blob() { g_prefs.putBytes("blob", &g_blob, sizeof(EncryptedBlob)); }
void save_attempts() { g_prefs.putInt("pinatt", g_pinAttempts); }
void apply_brightness() { ledcWrite(TFT_BL, BRI_LEVELS[g_briIdx]); }

void factory_reset() {
  g_prefs.clear();              // apaga blob, pinatt, bri do namespace claude
  g_wifi.forgetAll();
  g_hasToken = false;
  g_token[0] = 0; g_pendingToken[0] = 0;
  g_pinAttempts = 0;
  g_onboarding = true;
  Serial.println("[RESET] tudo apagado");
}
