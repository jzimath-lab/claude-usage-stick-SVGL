/**
 * app_state.cpp — definicao unica dos globais declarados em app_state.h.
 *
 * Cada simbolo aqui existe exatamente uma vez no binario. Se algum voltar a ser
 * `static` ou for definido tambem em outro .cpp, o erro aparece so na linkagem
 * ("multiple definition of...") e aponta para o arquivo errado.
 */
#include "app_state.h"

// ---- Idioma ----
uint8_t g_lang = 0;

// ---- Hardware ----
Arduino_Canvas *gfx        = nullptr;
uint16_t       *canvas_fb  = nullptr;
AXS15231B_Touch touch_dev(TOUCH_SCL, TOUCH_SDA, TOUCH_INT, TOUCH_ADDR, TOUCH_ROTATION);
WiFiManager     g_wifi;
Preferences     g_prefs;

// ---- Estado da aplicacao ----
State g_state   = ST_BOOT;
State g_pending = ST_BOOT;
bool  g_dirty   = false;

// ---- Dados ----
UsageData   g_usage  = {};
CodexUsage  g_codex  = {};
GithubUsage g_github = {};
ModelStatus g_status = {true, true, true, true, false};

// ---- Modelos sondados ----
ModelInfo g_models[NMODELS] = {
  {"Haiku",  "claude-haiku-4-5-20251001", {0, 0}, 0},
  {"Sonnet", "claude-sonnet-5",           {0, 0}, 0},
  {"Opus",   "claude-opus-4-8",           {0, 0}, 0},
  {"Fable",  "claude-fable-5",            {0, 0}, 0},
};
int g_probeIdx = 0;

// ---- Tokens por sessao ----
TokenStats g_tok = {0, 0, 0, 0, 0};

// ---- Token / seguranca ----
EncryptedBlob g_blob;
VpsCreds      g_vps;
bool     g_hasToken               = false;
bool     g_onboarding             = false;
char     g_token[200]             = {0};
char     g_pendingToken[200]      = {0};
char     g_pinEntry[PIN_LEN + 1]  = {0};
char     g_pinFirst[PIN_LEN + 1]  = {0};
bool     g_pinConfirming          = false;
int      g_pinAttempts            = 0;
uint32_t g_lockoutUntil           = 0;
bool     g_timeInit               = false;

// ---- Refresh em background ----
bool      g_wantRefresh = false;
bool      g_refreshing  = false;
bool      g_lastFetchOk = true;
uint32_t  g_lastOkMs    = 0;
lv_obj_t *g_hdrStatus   = nullptr;

// ---- Brilho ----
const uint8_t BRI_LEVELS[3] = {60, 160, 255};
int           g_briIdx      = 1;

// ---- Configuracao (persistida em NVS) ----
uint32_t g_lastPollMs  = 0;
int      g_pollSec     = DEFAULT_POLL_SEC;
int      g_tzOffset    = -3;
int      g_slideSec    = 0;
int      g_heatMode    = 3;
uint32_t g_lastTouchMs = 0;
uint32_t g_lastSlideMs = 0;

// ---- Historico ----
Sample g_hist[HIST_MAX];
int    g_histN      = 0;
int    g_histHead   = 0;
float  g_hourBurn[24] = {0};
float  g_lastH5     = -1.0f;

// ---- Heatmap por dia ----
DayHeat g_days[NDAYS];
int     g_dayN = 0;

// ---- Mascotes ----
Mascot             g_masc[NMODELS];
int                g_mascN = 0;
lv_point_precise_t g_mXPts[NMODELS][4][2];

// ---- Ponteiros de UI do dashboard ----
DashUI    g_ui;
lv_obj_t *g_pinDots = nullptr;
lv_obj_t *g_pinMsg  = nullptr;
int       g_curTile = 0;

lv_point_precise_t g_trPts[HIST_MAX];
lv_point_precise_t g_trProjPts[2];
