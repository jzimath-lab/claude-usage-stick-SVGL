/**
 * app_state.h — tipos, constantes e estado global compartilhado entre os modulos.
 *
 * Extraido do claude_stick.ino (linhas 17-169) na quebra do monolito (ZYN-379).
 *
 * IMPORTANTE: no .ino original tudo era `static`, o que funciona porque havia um
 * unico arquivo. Ao dividir, `static` passaria a criar uma copia por .cpp — cada
 * modulo enxergaria um estado diferente e a falha apareceria em runtime, nao na
 * compilacao. Por isso cada global e declarado `extern` aqui e definido UMA vez
 * em app_state.cpp.
 *
 * logo_assets.h NAO entra aqui de proposito: ele carrega os dados das imagens e
 * so deve ser incluido pelo modulo que efetivamente desenha os mascotes.
 */
#ifndef APP_STATE_H
#define APP_STATE_H

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <time.h>
#include <math.h>
#include "config.h"
#include "touch.h"
#include "wifi_manager.h"
#include "api.h"
#include "status.h"
#include "crypto.h"

// ---- Paleta (escuro, minimalista; acento coral do Claude) ----
#define C_BG       0x0F0F12
#define C_SURFACE  0x1A1A20   // cards sem borda
#define C_SURFACE2 0x24242C   // teclas / botoes secundarios
#define C_TRACK    0x26262E   // trilho de barras
#define C_GRID     0x232329   // linhas de grade dentro de cards
#define C_BORDER   0x30303A   // hairlines raras
#define C_TEXT     0xF2F0EC
#define C_MUTED    0x8C8C98
#define C_FAINT    0x5C5C68
#define C_ACCENT   0xD97757   // coral Claude
#define C_OK       0x4ADE80
#define C_WARN     0xFBBF24
#define C_BAD      0xF87171

// ---- Constantes de dimensionamento ----
#define NMODELS      4
#define NTILES       4
#define NSEG         18                        // segmentos do medidor de janela
#define HIST_MAX     160
#define NDAYS        31
#define TOK_FRESH_MS (15UL * 60UL * 1000UL)

// ---- Idioma (0 = portugues, 1 = english; Ajustes -> NVS "lang") ----
extern uint8_t g_lang;
#define TRS(pt, en) (g_lang ? (en) : (pt))

// ---- Tipos ----
enum State {
  ST_BOOT, ST_PIN, ST_SETUP_PIN, ST_WIFI, ST_TOKEN,
  ST_LOADING, ST_MAIN, ST_SETTINGS, ST_ABOUT, ST_ERROR
};

struct ModelInfo  { const char *name; const char *id; ProbeResult pr; uint32_t atMs; };
struct TokenStats { long long tin, tout, cache; int sessions; uint32_t atMs; };
struct Sample     { uint32_t t; uint8_t h5; uint8_t d7; };   // t = epoch (0 = relogio nao sincronizado)
struct DayHeat    { uint32_t day; float burn[24]; };         // day = dias locais desde epoch

// mood: 0=nunca sondado, 1=ok, 2=limitado(429), 3=erro/incidente, 4=n/d(404)
struct Mascot { lv_obj_t *cont, *img, *lid[2], *drop; int baseY, mood; };

struct DashUI {
  lv_obj_t *tv, *tile[NTILES], *dots[NTILES];
  lv_obj_t *refBar;
  // agora (overview + reset mesclados)
  lv_obj_t *agChip, *agPct5, *agCd5, *agAt5;
  lv_obj_t *agPct7, *agCd7, *agAt7, *agTok;
  lv_obj_t *seg5[NSEG], *seg7[NSEG];  // medidores segmentados
  // modelos
  lv_obj_t *mChip[NMODELS], *incident;
  // tendencia da janela 5h (linhas custom)
  lv_obj_t *trHist, *trProj, *trDot, *trCap, *trT0, *trT1;
  // ritmo por hora
  lv_obj_t *heat[24], *heatBtn[4];
};

// ---- Hardware ----
extern Arduino_Canvas    *gfx;
extern uint16_t          *canvas_fb;
extern AXS15231B_Touch    touch_dev;
extern WiFiManager        g_wifi;
extern Preferences        g_prefs;

// ---- Estado da aplicacao ----
extern State g_state;
extern State g_pending;
extern bool  g_dirty;

inline void request_state(State s) { g_pending = s; g_dirty = true; }

// ---- Dados ----
extern UsageData   g_usage;
extern ModelStatus g_status;

// ---- Modelos sondados (1 por ciclo, rotativo) ----
extern ModelInfo g_models[NMODELS];
extern int       g_probeIdx;

// ---- Tokens por sessao (vindos do bridge via POST /tokens) ----
extern TokenStats g_tok;

// ---- Token / seguranca ----
extern EncryptedBlob g_blob;
extern bool     g_hasToken;                 // existe blob salvo no NVS
extern bool     g_onboarding;               // primeiro setup em andamento
extern char     g_token[200];               // token decifrado (so em RAM)
extern char     g_pendingToken[200];        // token digitado, aguardando PIN
extern char     g_pinEntry[PIN_LEN + 1];    // digitos sendo digitados
extern char     g_pinFirst[PIN_LEN + 1];    // 1a entrada no setup de PIN
extern bool     g_pinConfirming;            // setup: confirmando 2a vez
extern int      g_pinAttempts;              // tentativas erradas (persistido)
extern uint32_t g_lockoutUntil;             // millis ate liberar nova tentativa
extern bool     g_timeInit;

// ---- Refresh em background ----
extern bool      g_wantRefresh;   // botao de refresh pediu atualizacao
extern bool      g_refreshing;    // busca em andamento
extern bool      g_lastFetchOk;   // ultimo fetch deu certo?
extern uint32_t  g_lastOkMs;      // millis do ultimo sucesso (p/ "atualizado ha Xs")
extern lv_obj_t *g_hdrStatus;     // texto de status no cabecalho do dashboard

// ---- Brilho ----
extern const uint8_t BRI_LEVELS[3];
extern int           g_briIdx;

// ---- Configuracao (persistida em NVS) ----
extern uint32_t g_lastPollMs;   // millis do ultimo poll (p/ barra de refresh)
extern int      g_pollSec;      // intervalo de atualizacao
extern int      g_tzOffset;     // fuso GMT (horas)
extern int      g_slideSec;     // slideshow: 0=off, 5/10/15/30s
extern int      g_heatMode;     // 0=hoje 1=7d 2=30d 3=tudo
extern uint32_t g_lastTouchMs;  // ultimo toque (pausa o slideshow)
extern uint32_t g_lastSlideMs;

// ---- Historico (ring buffer; persistido em LittleFS) ----
extern Sample g_hist[HIST_MAX];
extern int    g_histN;
extern int    g_histHead;
extern float  g_hourBurn[24];   // consumo por hora do dia (todo o tempo)
extern float  g_lastH5;         // ultima utilizacao 5h (delta do heatmap)

// ---- Heatmap por dia (filtro hoje/7d/30d) ----
extern DayHeat g_days[NDAYS];
extern int     g_dayN;

// ---- Mascotes Clawd (pagina de modelos; humor por status) ----
extern Mascot              g_masc[NMODELS];
extern int                 g_mascN;
extern lv_point_precise_t  g_mXPts[NMODELS][4][2];   // olhos em X (mood 3)

// ---- Ponteiros de UI do dashboard (zerados a cada build de ST_MAIN) ----
extern DashUI    g_ui;
extern lv_obj_t *g_pinDots;
extern lv_obj_t *g_pinMsg;
extern int       g_curTile;

// pontos das linhas do grafico de tendencia (precisam persistir)
extern lv_point_precise_t g_trPts[HIST_MAX];
extern lv_point_precise_t g_trProjPts[2];

#endif // APP_STATE_H
