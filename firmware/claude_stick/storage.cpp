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
// Brilho noturno — regra do design 7.1.
//
// E FUNCAO DO HORARIO, nao gatilho de borda: avaliada a cada tick. Implementar
// por evento deixaria o aparelho claro a noite inteira se ele ligasse as 23h ou
// reconectasse as 2h depois de queda de energia — e queda de energia e o modo
// de falha conhecido deste device.
//
// O guard g_timeInit e essencial: sem NTP, localtime() de um relogio zerado
// devolve 1970 (hora 00), a regra daria "noite" e o aparelho NASCERIA ESCURO no
// boot, antes do NTP responder — indistinguivel de tela morta, justamente no
// aparelho cuja funcao e avisar.
static bool g_briSuspenso = false;   // ajuste manual a noite suspende ate o proximo 21h
static int  g_briUltimo   = -1;

static bool bri_noite() {
  if (!g_timeInit) return false;
  time_t agora = time(nullptr);
  struct tm lt;
  localtime_r(&agora, &lt);
  // O intervalo CRUZA A MEIA-NOITE: com && nunca seria verdadeiro e o recurso
  // simplesmente nao aconteceria, sem erro nenhum.
  return (lt.tm_hour >= BRI_HORA_NOITE || lt.tm_hour < BRI_HORA_DIA);
}

void apply_brightness() {
  int nivel = (bri_noite() && !g_briSuspenso) ? BRI_NOITE : BRI_LEVELS[g_briIdx];
  if (nivel == g_briUltimo) return;          // evita reescrever o PWM a cada segundo
  ledcWrite(TFT_BL, nivel);
  g_briUltimo = nivel;
}

// Chamado a cada 1s pelo laco principal.
void bri_tick() {
  static bool eraNoite = false;
  bool n = bri_noite();
  if (!n && eraNoite) g_briSuspenso = false;   // amanheceu: rearma o automatico
  eraNoite = n;
  apply_brightness();
}

// Ajuste manual pelas settings. Dois escritores no mesmo pino: sem isto, mexer
// no brilho a noite seria desfeito em 1 s pelo bri_tick — a tela desobedecendo
// na frente do usuario. A suspensao vive em RAM: reboot volta ao automatico.
void bri_manual() {
  if (bri_noite()) g_briSuspenso = true;
  g_briUltimo = -1;
  apply_brightness();
}

void factory_reset() {
  g_prefs.clear();              // apaga blob, pinatt, bri do namespace claude
  g_wifi.forgetAll();
  g_hasToken = false;
  g_token[0] = 0; g_pendingToken[0] = 0;
  g_pinAttempts = 0;
  g_onboarding = true;
  Serial.println("[RESET] tudo apagado");
}
