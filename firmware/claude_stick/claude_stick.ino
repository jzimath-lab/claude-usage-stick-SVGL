/**
 * Claude Usage Stick — tela touch LVGL
 * Placa: Guition JC4832W535 (ESP32-S3, AXS15231B QSPI), 480x320 paisagem.
 *
 * Dashboard do rate-limit do Claude Code (janelas 5h e 7d, headers unified-*),
 * sonda real por modelo (latencia + HTTP), projecao de esgotamento da janela
 * 5h e ritmo de uso por hora com filtro de periodo. Token OAuth digitado na
 * tela e guardado cifrado (AES-256-GCM, chave derivada de um PIN de 4 digitos).
 *
 * Tokens por sessao: a API nao expoe contagem para conta de assinatura; um
 * bridge opcional (tools/token_bridge.py) soma os transcripts locais do
 * Claude Code e faz POST /tokens neste device (mDNS claude-stick.local).
 *
 * Sem botao fisico: navegacao 100% touch (swipe entre telas + slideshow).
 * Init de display/touch validado no bring-up (ver REFERENCIA-HARDWARE-LVGL.md).
 */
#include "app_state.h"
#include "codex_secrets.h"
#include "github_api.h"
#include "github_secrets.h"
#include "codex_history.h"
#include <esp_task_wdt.h>
#include "ui_pin.h"
#include "ui_wifi.h"
#include "web_server.h"
#include "usage_pull.h"
#include "codex_merge.h"
#include "ui_message.h"
#include "history.h"
#include "ui_dashboard.h"
#include "ui_main.h"
#include "ui_tiles.h"
#include "ui_moments.h"
#include "ui_refresh.h"
#include "ui_settings.h"
#include "logo_assets.h"   // Clawd + logotipo oficiais (gerado por tools/gen_logo_assets.py)

// ---- Forward declarations ----
static void render_state();
void refresh_ui_values();
void dash_tick();
void set_hdr_status();
void apply_tz();
void nav_cb(lv_event_t *e);
void start_data_web();

#include "display.h"
// ============================================================
#include "ui_helpers.h"

// ============================================================
#include "storage.h"

// ============================================================

void nav_cb(lv_event_t *e) {
  State s = (State)(intptr_t)lv_event_get_user_data(e);
  request_state(s);
}

// ============================================================
// Render do estado atual
// ============================================================
static void render_state() {
  g_state = g_pending;
  stop_web();                                 // cada tela sobe o servidor que precisa
  moment_close();                             // overlay vive em lv_layer_top
  lv_obj_clean(lv_layer_top());
  // invalida ponteiros vivos antes de destruir a tela antiga
  memset(&g_ui, 0, sizeof(g_ui));
  g_mascN = 0;
  ui_pin_invalidate();
  ui_token_invalidate();
  g_hdrStatus = nullptr;
  ui_settings_invalidate();

  lv_obj_clean(lv_screen_active());
  lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(C_BG), 0);
  lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

  switch (g_state) {
    case ST_PIN:
    case ST_SETUP_PIN: ui_pin(); break;
    case ST_WIFI:      ui_wifi(); break;
    case ST_TOKEN:     ui_token(); break;
    case ST_LOADING:   ui_loading(g_wifi.isConnected() ? g_wifi.getSSID().c_str()
                                                       : TRS("conectando WiFi", "connecting WiFi")); break;
    case ST_MAIN:      ui_main(); break;
    case ST_SETTINGS:  ui_settings(); break;
    case ST_ABOUT:     ui_about(); break;
    case ST_ERROR: {
      char sub[96]; error_sub(sub, sizeof(sub));
      ui_message(TRS("Falha", "Failed"), sub, C_BAD);
      // ⚠️ O servidor de dados sobe AQUI TAMBEM, e nao so nas telas de sucesso.
      //
      // Ele era iniciado apenas em ui_main() e ui_settings(); como o ciclo faz
      // request_state(ok ? ST_MAIN : ST_ERROR), uma falha de busca deixava o
      // aparelho nesta tela com o servidor DESLIGADO — e o formulario /vps,
      // que existe justamente para trocar a fonte quando a busca falha, ficava
      // inalcancavel exatamente no estado em que e necessario.
      //
      // Dependencia circular: a ferramenta de recuperacao exigindo o estado que
      // ela deveria recuperar. Mesma forma do runbook cujo procedimento de ler
      // o serial estava quebrado, e do lockout que apaga o token de quem erra o
      // PIN tentando entrar.
      //
      // O servidor e canal de CONFIGURACAO, nao acabamento da tela de sucesso:
      // deve existir sempre que o aparelho esteja destravado e na rede.
      start_data_web();
      break;
    }
    default: break;
  }
}

// ============================================================
// Tempo (NTP) e ciclo de dados
// ============================================================
void apply_tz() { configTime(g_tzOffset * 3600, 0, NTP_SERVER_1, NTP_SERVER_2); }
static void ensure_time() {
  if (g_timeInit || !g_wifi.isConnected()) return;
  apply_tz();
  g_timeInit = true;
  Serial.println("[NTP] sync iniciado");
}

// ---- Backoff em falhas consecutivas ----
// Sem isso o device tenta no mesmo intervalo durante uma queda inteira. Cada
// tentativa BLOQUEIA o loop por ate API_TIMEOUT_MS (15s), entao um outage longo
// vira uma UI que congela 15s a cada 2min. Dobrar o intervalo reduz o incomodo
// sem abrir mao de reconectar sozinho.
static int g_failStreak = 0;

static uint32_t next_poll_ms() {
  uint32_t base = (uint32_t)g_pollSec * 1000;
  if (g_failStreak <= 0) return base;
  int shift = g_failStreak > 3 ? 3 : g_failStreak;      // x2, x4, x8 e para
  uint32_t d = base << shift;
  const uint32_t MAX_BACKOFF_MS = 15UL * 60UL * 1000UL;  // teto de 15 min
  return d > MAX_BACKOFF_MS ? MAX_BACKOFF_MS : d;
}

// Texto do subtitulo da tela de erro: diz que vai tentar de novo sozinho.
static void error_sub(char *out, int sz) {
  uint32_t s = next_poll_ms() / 1000;
  if (g_usage.error[0])
    snprintf(out, sz, "%s - %s %us", g_usage.error, TRS("nova tentativa em", "retrying in"), (unsigned)s);
  else
    snprintf(out, sz, "%s %us", TRS("nova tentativa em", "retrying in"), (unsigned)s);
}

// Busca o Codex pelo bridge e guarda em g_codex (usado no 1o load e no bg).
/*
 * ⚠️ AS DUAS FONTES SE COMPLEMENTAM — nao substituem uma a outra.
 *
 * A estacao entrega janelas, plano e custo. As ANALYTICS (interacoes, creditos,
 * origem do consumo e modelo consumido — as tres telas do Codex) so existem no
 * bridge proprio: o payload do /api/display nao as carrega.
 *
 * A primeira versao desta consolidacao dava `return` apos o pull e MATAVA
 * aquelas tres telas. Nenhum portao pegou: os testes de host passavam, o
 * firmware compilava, e o log dizia "[CODEX] via estacao" com ar de sucesso. O
 * sintoma so aparecia numa tela que a verificacao nao abriu.
 *
 * Consolidar deve reduzir requisicoes, nao funcionalidade.
 */
static void fetch_codex() {
  CodexUsage cx;
  bool viaEstacao = pullCodex(cx);   /* de graca: o corpo ja foi baixado */

  CodexUsage bridge;
  bool viaBridge = fetchCodexUsage(CODEX_USAGE_URL, CODEX_BASIC_B64, CODEX_BRIDGE_TOKEN, bridge);

  /* A combinacao (e a frescura de cada metade) mora em codex_merge.h, testada
   * no host — aqui so ficam a I/O e o log. */
  CodexUsage out;
  if (!codexCombinar(viaBridge ? &bridge : nullptr,
                     viaEstacao ? &cx : nullptr,
                     g_codex, out)) {
    /* As DUAS falharam: nao zera g_codex — o §7 manda o dado morto continuar
     * na tela, apagado, em vez de sumir. */
    Serial.printf("[CODEX] as duas fontes falharam: %s\n", bridge.error);
    return;
  }

  g_codex = out;
  if (g_codex.has7d) { cx_hist_push(g_codex.pct7); cx_save_history(); }

  /* O log distingue as duas frescuras: foi olhando "an=1" com a tela errada
   * que a regressao anterior escapou. Sucesso parcial precisa ser legivel. */
  Serial.printf("[CODEX] janelas=%s analytics=%s 5h=%s%.1f 7d=%s%.1f plan=%s an=%d cotas=%s an=%s\n",
    viaEstacao ? "estacao" : (viaBridge ? "bridge" : "-"),
    viaBridge ? "bridge" : "ciclo anterior",
    g_codex.has5h?"":"-", g_codex.pct5, g_codex.has7d?"":"-", g_codex.pct7,
    g_codex.plan, (int)g_codex.hasAn,
    g_codex.stale ? "DATADAS" : "frescas",
    g_codex.anStale ? "DATADAS" : "frescas");
}

// Busca o consumo do GitHub na estacao e guarda em g_github.
static void fetch_github() {
  // static, nao local: a struct tem ~850 bytes e a pilha desta tarefa ja
  // carrega o WiFiClientSecure com o handshake TLS. Continua sendo copia
  // separada de g_github para nao apagar o ultimo-bom quando a busca falha.
  static GithubUsage gh;
  // Idem: extraido do corpo que o pull ja tem. E o parser e o MESMO ghParse —
  // as duas formas sao identicas, porque o vhost `github-usage` serve o mesmo
  // ghPainel.montar() que a estacao.
  if (pullGithub(gh)) {
    g_github = gh;
    Serial.printf("[GITHUB] via estacao fonte=%s cota=%u%% usd=%.2f proj=%u\n",
      gh.billing ? "billing" : "calculado", gh.pct, (double)gh.usd, gh.nProj);
    return;
  }
  if (fetchGithubUsage(GITHUB_URL, GITHUB_BASIC_B64, GITHUB_DEVICE_TOKEN, gh)) {
    g_github = gh;
    Serial.printf("[GITHUB] ok fonte=%s cota=%u%% usd=%.2f proj=%u dias=%u\n",
      gh.billing ? "billing" : "calculado", gh.pct, (double)gh.usd, gh.nProj, gh.nDay);
  } else {
    Serial.printf("[GITHUB] fetch falhou: %s\n", gh.error);
  }
}

// Sonda o próximo modelo da rotação.
static void probe_next_model() {
  int mi = g_probeIdx % NMODELS;
  g_probeIdx++;
  probeModel(g_token, g_models[mi].id, g_models[mi].pr);
  g_models[mi].atMs = millis();
}

// Primeiro load (mostra a tela de carregamento). Vai p/ ST_MAIN ou ST_ERROR.
// O refresh e uma sequencia de requisicoes HTTPS BLOQUEANTES, e o
// esp_task_wdt_reset() so acontece no topo do loop(). Ou seja: durante todo o
// do_refresh() o watchdog NAO e alimentado.
//
// Sao cinco chamadas, cada uma com API_TIMEOUT_MS=15s de teto: no pior caso
// 75 s contra um WDT de 90 s — e no primeiro carregamento tudo esta frio (NTP,
// handshakes TLS com o bundle inteiro de CAs). O reset por watchdog e
// SILENCIOSO: nao imprime backtrace, e o sintoma vira "trava no carregando e
// reinicia", que parece crash e nao e.
//
// A marca de serial antes de cada etapa e o que permite localizar QUAL delas
// pendurou, ja que nao ha screenshot de hardware.
#define WDT_ETAPA(nome) do { esp_task_wdt_reset(); \
  Serial.printf("[REFRESH] %-14s heap=%u maior=%u psram=%u\n", nome, \
    (unsigned)ESP.getFreeHeap(), \
    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), \
    (unsigned)ESP.getFreePsram()); } while (0)

static void do_refresh() {
  WDT_ETAPA("ntp");
  ensure_time();
  WDT_ETAPA("claude/usage");
  bool ok = obterUsage(g_usage);
  if (ok) {
    WDT_ETAPA("claude/status");
    fetchModelStatus(g_status); g_lastOkMs = millis(); g_lastFetchOk = true;
    hist_push(g_usage.h5, g_usage.d7); accumulate_heat(g_usage.h5); save_history();
    check_thresholds();
    WDT_ETAPA("claude/probe");
    probe_next_model();
    g_failStreak = 0;
  } else { g_lastFetchOk = false; if (g_failStreak < 99) g_failStreak++; }
  WDT_ETAPA("codex");
  fetch_codex();
  WDT_ETAPA("github");
  fetch_github();
  WDT_ETAPA("fim");
  g_lastPollMs = millis();
  request_state(ok ? ST_MAIN : ST_ERROR);
}

// Atualização EM BACKGROUND: não troca de tela; mantém o dashboard e os dados
// antigos se falhar. A chamada à API é bloqueante (~1-2s), então mostra
// "atualizando..." no cabeçalho durante a busca.
static void bg_refresh() {
  if (!g_wifi.isConnected()) g_wifi.autoConnect(WIFI_CONNECT_TIMEOUT_MS);
  ensure_time();
  g_refreshing = true; set_hdr_status(); lv_refr_now(NULL);
  UsageData u = {};
  bool ok = obterUsage(u);
  bool rebuild = false;
  if (ok) {
    g_usage = u; g_lastOkMs = millis(); g_lastFetchOk = true;
    hist_push(u.h5, u.d7); accumulate_heat(u.h5); save_history();
    check_thresholds();
    int moodBefore[NMODELS];
    for (int i = 0; i < NMODELS; i++) moodBefore[i] = model_mood(i);
    fetchModelStatus(g_status);
    probe_next_model();
    for (int i = 0; i < NMODELS; i++)
      if (moodBefore[i] != model_mood(i)) rebuild = true;   // mascote muda de humor
    g_failStreak = 0;
  } else { g_lastFetchOk = false; if (g_failStreak < 99) g_failStreak++; }
  fetch_codex();   // 2o provider, independente do resultado do Claude
  fetch_github();  // 3o provider (estacao); tambem independente
  g_refreshing = false;
  g_lastPollMs = millis();
  if (rebuild) request_state(ST_MAIN);    // mascotes mudaram -> rebuild
  else refresh_ui_values();               // resto: in-place (preserva o tile atual)
}

// ============================================================
// setup / loop
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== Claude Usage Stick (touch) ===");

  // Watchdog: o core do Arduino ja inicializa o TWDT, entao aqui e reconfigurar
  // (init retornaria INVALID_STATE) e inscrever a task do loop.
  esp_task_wdt_config_t wdtCfg = { .timeout_ms = WDT_TIMEOUT_MS, .idle_core_mask = 0, .trigger_panic = true };
  esp_task_wdt_reconfigure(&wdtCfg);
  esp_task_wdt_add(NULL);
  Serial.printf("[WDT] armado em %d ms\n", WDT_TIMEOUT_MS);

  // Display
  Arduino_DataBus *bus = new Arduino_ESP32QSPI(TFT_CS, TFT_SCK, TFT_SDA0, TFT_SDA1, TFT_SDA2, TFT_SDA3);
  Arduino_GFX *g = new Arduino_AXS15231B(bus, GFX_NOT_DEFINED, 0, false, 320, 480);
  gfx = new Arduino_Canvas(320, 480, g, 0, 0, 0);
  if (!gfx->begin(QSPI_FREQ)) { Serial.println("FATAL display — reiniciando em 3s"); delay(3000); ESP.restart(); }
  gfx->fillScreen(0x0000); gfx->flush();
  canvas_fb = gfx->getFramebuffer();

  // Backlight via PWM (brilho ajustável)
  ledcAttach(TFT_BL, 5000, 8);
  touch_dev.begin();

  // LVGL
  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  uint32_t bufSize = SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(lv_color_t);
  lv_color_t *buf = (lv_color_t *)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) { Serial.println("FATAL PSRAM — reiniciando em 3s"); delay(3000); ESP.restart(); }
  lv_display_t *disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  lv_display_set_flush_cb(disp, disp_flush_cb);
  lv_display_set_buffers(disp, buf, NULL, bufSize, LV_DISPLAY_RENDER_MODE_FULL);
  lv_indev_t *indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read_cb);

  load_persisted();
  apply_brightness();

  if (!LittleFS.begin(true)) Serial.println("LittleFS: falhou");
  else { load_history(); cx_load_history(); }

  g_wifi.begin();

  if (g_hasToken) {
    // Tenta WiFi cedo (em paralelo o usuário digita o PIN)
    g_wifi.autoConnect(WIFI_CONNECT_TIMEOUT_MS);
    request_state(ST_PIN);
  } else {
    g_onboarding = true;
    // Se já há WiFi salvo (reboot no meio do onboarding), pula direto p/ o token
    request_state(g_wifi.autoConnect(WIFI_CONNECT_TIMEOUT_MS) ? ST_TOKEN : ST_WIFI);
  }
}

void loop() {
  esp_task_wdt_reset();
  lv_task_handler();

  // Servidor web (token no onboarding; /window + /tokens no dashboard)
  web_pump();
  if (g_state == ST_TOKEN && web_token_arrived()) request_state(ST_SETUP_PIN);

  if (g_dirty) {
    g_dirty = false;
    render_state();
    if (g_state == ST_LOADING) {
      lv_task_handler();
      lv_refr_now(NULL);
      do_refresh();
    }
  }

  // Poll automático EM BACKGROUND (sem trocar de tela) + refresh manual.
  // ST_ERROR entra aqui de proposito: sem isso a tela de "Falha" e um beco sem
  // saida — ela nao tem botao, e so o ST_MAIN disparava refresh. Uma queda de
  // rede no primeiro load deixava o device travado ate reboot manual.
  if ((g_state == ST_MAIN || g_state == ST_ERROR) &&
      (g_wantRefresh || millis() - g_lastPollMs > next_poll_ms())) {
    g_wantRefresh = false;
    if (g_state == ST_ERROR) {
      g_lastPollMs = millis();      // evita reentrar antes do do_refresh rodar
      request_state(ST_LOADING);    // reusa o fluxo normal: LOADING -> MAIN ou ERROR
    } else {
      bg_refresh();                 // seta g_lastPollMs no fim
    }
  }

  // Atualização viva: contadores (1s), barra de refresh (250ms), mascotes,
  // slideshow (5s, pausa 10s após qualquer toque)
  if (g_state == ST_MAIN) {
    uint32_t now = millis();
    static uint32_t lastTick = 0, lastBar = 0, lastBob = 0, blinkAt = 0;
    static bool blinkClosed = false;
    if (now - lastTick > 1000) { lastTick = now; dash_tick(); update_tok_row(); bri_tick(); }
    if (now - lastBar > 250 && g_ui.refBar) {
      lastBar = now;
      int v;
      if (g_refreshing) v = 1000;
      else {
        uint32_t el = now - g_lastPollMs, per = (uint32_t)g_pollSec * 1000;
        v = el >= per ? 0 : (int)(1000 - (uint64_t)el * 1000 / per);
      }
      lv_bar_set_value(g_ui.refBar, v, LV_ANIM_OFF);
    }
    if (now - lastBob > 80) {                       // animação por humor
      lastBob = now;
      float ph = now / 600.0f;
      for (int i = 0; i < g_mascN; i++) {
        if (!g_masc[i].cont) continue;
        if (g_masc[i].mood == 1)                    // ok: bob alegre
          lv_obj_set_y(g_masc[i].cont, g_masc[i].baseY + (int)(2.0f * sinf(ph + i * 0.9f) - 1.0f));
        else if (g_masc[i].mood == 2) {             // limitado: bob curto + suor
          lv_obj_set_y(g_masc[i].cont, g_masc[i].baseY + (int)(1.2f * sinf(ph * 0.6f + i)));
          if (g_masc[i].drop) {
            uint32_t cyc = (now + i * 300) % 900;
            lv_obj_set_y(g_masc[i].drop, 24 + (int)(cyc * 22 / 900));
            lv_obj_set_style_bg_opa(g_masc[i].drop, (lv_opa_t)(255 - cyc * 190 / 900), 0);
          }
        }
      }
    }
    uint32_t bp = blinkClosed ? 150 : 3000;
    if (now - blinkAt > bp) {                        // piscar (só quem está ok)
      blinkAt = now; blinkClosed = !blinkClosed;
      for (int i = 0; i < g_mascN; i++) {
        if (g_masc[i].mood != 1) continue;
        for (int k = 0; k < 2; k++) {
          if (!g_masc[i].lid[k]) continue;
          if (blinkClosed) lv_obj_clear_flag(g_masc[i].lid[k], LV_OBJ_FLAG_HIDDEN);
          else             lv_obj_add_flag(g_masc[i].lid[k], LV_OBJ_FLAG_HIDDEN);
        }
      }
    }
    if (g_slideSec > 0 && g_ui.tv && !g_refreshing && !moment_active() &&
        now - g_lastTouchMs > 10000 && now - g_lastSlideMs > (uint32_t)g_slideSec * 1000) {
      g_lastSlideMs = now;
      // Rotaciona os TRES "Agora": Claude -> Codex -> GitHub -> Claude.
      int next = (g_curTile == 0) ? CODEX_TILE
               : (g_curTile == CODEX_TILE) ? GITHUB_TILE : 0;
      lv_tileview_set_tile_by_index(g_ui.tv, next, 0, LV_ANIM_ON);
    }

    // Momentos de limiar: mostra pendente e anima o overlay ativo
    moment_pump(g_refreshing);
  }

  delay(5);
}
