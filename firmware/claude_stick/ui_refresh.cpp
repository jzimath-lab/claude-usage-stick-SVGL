#include "ui_refresh.h"
#include "cota_sev.h"
#include "codex_history.h"
#include "ui_dashboard.h"
#include "ui_moments.h"

// Ainda no claude_stick.ino; migram para ui_tiles.*
int tr_x(uint32_t tt, uint32_t ws, uint32_t we);
int tr_y(float p);
int hist_idx(int i);
void heat_mode_data(int mode, float out[24]);

// Atualização de valores
// ============================================================
// Estados possiveis desta linha:
//   nunca recebeu  -> vazio (o bridge e opcional; nao vale poluir quem nao usa)
//   fresco (<15m)  -> valores em C_MUTED
//   velho  (>15m)  -> MESMOS valores em C_FAINT + idade explicita
//
// Antes o caso "velho" apagava a linha. Isso confundia duas situacoes muito
// diferentes — "voce nao configurou o bridge" e "o bridge parou de responder" —
// e ainda jogava fora um numero que continua util, so que datado.
void update_tok_row() {
  if (!g_ui.agTok) return;
  if (g_tok.atMs == 0) {                       // bridge nunca falou com o device
    lv_label_set_text(g_ui.agTok, "");
    return;
  }
  uint32_t age = millis() - g_tok.atMs;
  bool stale = age > TOK_FRESH_MS;

  char a[16], b[16], s[128];
  fmt_tok(g_tok.tin, a, sizeof(a));
  fmt_tok(g_tok.tout, b, sizeof(b));
  if (stale) {
    unsigned mins = age / 60000UL;
    if (mins > 999) mins = 999;
    snprintf(s, sizeof(s), TRS("tokens (ha %um): %s entrada \xE2\x80\xA2 %s saida",
                               "tokens (%um ago): %s in \xE2\x80\xA2 %s out"), mins, a, b);
  } else {
    snprintf(s, sizeof(s), TRS("tokens na janela: %s entrada \xE2\x80\xA2 %s saida",
                               "window tokens: %s in \xE2\x80\xA2 %s out"), a, b);
  }
  lv_label_set_text(g_ui.agTok, s);
  lv_obj_set_style_text_color(g_ui.agTok, lv_color_hex(stale ? C_FAINT : C_MUTED), 0);
}

// Contadores/relógios (1s) — separado dos valores de fetch.
void dash_tick() {
  if (g_state != ST_MAIN || !g_ui.agCd5) return;
  char e[32], c[24], b[64];
  fmt_eta(g_usage.h5ResetEpoch, e, sizeof(e));
  lv_label_set_text(g_ui.agCd5, e);
  fmt_clock(g_usage.h5ResetEpoch, c, sizeof(c));
  snprintf(b, sizeof(b), TRS("RESETA EM \xE2\x80\xA2 %s", "RESETS \xE2\x80\xA2 %s"), c);
  lv_label_set_text(g_ui.agAt5, b);

  fmt_eta(g_usage.d7ResetEpoch, e, sizeof(e));
  lv_label_set_text(g_ui.agCd7, e);
  fmt_clock(g_usage.d7ResetEpoch, c, sizeof(c));
  snprintf(b, sizeof(b), TRS("RESETA EM \xE2\x80\xA2 %s", "RESETS \xE2\x80\xA2 %s"), c);
  lv_label_set_text(g_ui.agAt7, b);

  if (g_ui.cxCd7) {                       // Codex Agora: só o countdown da SEMANA (5h aposentado)
    fmt_eta(g_codex.reset7Epoch, e, sizeof(e));
    lv_label_set_text(g_ui.cxCd7, g_codex.has7d ? e : "--");
    fmt_clock(g_codex.reset7Epoch, c, sizeof(c));
    snprintf(b, sizeof(b), TRS("RESETA EM \xE2\x80\xA2 %s", "RESETS \xE2\x80\xA2 %s"), c);
    lv_label_set_text(g_ui.cxAt7, g_codex.has7d ? b : "");
  }
  set_hdr_status();
}

// Tendência da janela 5h: histórico + projeção pontilhada até esgotar.
void trend_redraw() {
  if (!g_ui.trHist) return;
  time_t now = time(nullptr);
  uint32_t we = g_usage.h5ResetEpoch;
  bool clockOk = (now > 1000000000L) && we != 0;

  if (!clockOk) {
    lv_line_set_points(g_ui.trHist, g_trPts, 0);
    lv_line_set_points(g_ui.trProj, g_trProjPts, 0);
    lv_obj_add_flag(g_ui.trDot, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_ui.trCap, TRS("Aguardando dados da janela...", "Waiting for window data..."));
    lv_obj_set_style_text_color(g_ui.trCap, lv_color_hex(C_MUTED), 0);
    return;
  }
  uint32_t ws = we - 5 * 3600;

  // horários do eixo
  char t0[12], t1[12], b[112];
  fmt_hm(ws, t0, sizeof(t0)); fmt_hm(we, t1, sizeof(t1));
  lv_label_set_text(g_ui.trT0, t0);
  lv_label_set_text(g_ui.trT1, t1);

  // pontos do histórico dentro da janela
  int n = 0;
  for (int i = 0; i < g_histN && n < HIST_MAX; i++) {
    Sample s = g_hist[hist_idx(i)];
    if (s.t == 0 || s.t < ws || s.t > (uint32_t)now) continue;
    g_trPts[n].x = tr_x(s.t, ws, we);
    g_trPts[n].y = tr_y(s.h5);
    n++;
  }
  // ponto atual (leitura mais recente)
  uint32_t nowClamped = ((uint32_t)now > we) ? we : (uint32_t)now;
  if (n < HIST_MAX) {
    g_trPts[n].x = tr_x(nowClamped, ws, we);
    g_trPts[n].y = tr_y(g_usage.h5);
    n++;
  }
  lv_line_set_points(g_ui.trHist, g_trPts, n);

  int cx = tr_x(nowClamped, ws, we), cy = tr_y(g_usage.h5);
  lv_obj_set_pos(g_ui.trDot, cx - 4, cy - 4);
  lv_obj_clear_flag(g_ui.trDot, LV_OBJ_FLAG_HIDDEN);

  if (n < 3) {
    lv_line_set_points(g_ui.trProj, g_trProjPts, 0);
    lv_label_set_text(g_ui.trCap, TRS("Coletando dados... (~alguns minutos)",
                                      "Collecting data... (~a few minutes)"));
    lv_obj_set_style_text_color(g_ui.trCap, lv_color_hex(C_MUTED), 0);
    return;
  }

  // taxa: janela dos últimos 45 min
  float rate = 0;                    // %/min
  {
    Sample first = {0, 0, 0};
    for (int i = 0; i < g_histN; i++) {
      Sample s = g_hist[hist_idx(i)];
      if (s.t == 0 || s.t < ws) continue;
      if (s.t >= (uint32_t)now - 2700) { first = s; break; }
    }
    if (first.t != 0 && (uint32_t)now > first.t + 300) {
      float dt = ((uint32_t)now - first.t) / 60.0f;
      rate = (g_usage.h5 - first.h5) / dt;
    }
  }

  char e[32];
  if (g_usage.h5 >= 99.5f) {
    lv_line_set_points(g_ui.trProj, g_trProjPts, 0);
    fmt_eta(we, e, sizeof(e));
    snprintf(b, sizeof(b), TRS("Janela esgotada \xE2\x80\xA2 reseta em %s",
                               "Window exhausted \xE2\x80\xA2 resets in %s"), e);
    lv_label_set_text(g_ui.trCap, b);
    lv_obj_set_style_text_color(g_ui.trCap, lv_color_hex(C_BAD), 0);
  } else if (rate > 0.02f) {
    float minsLeft = (100.0f - g_usage.h5) / rate;
    uint32_t etaT = (uint32_t)now + (uint32_t)(minsLeft * 60);
    g_trProjPts[0].x = cx; g_trProjPts[0].y = cy;
    if (etaT <= we) {
      g_trProjPts[1].x = tr_x(etaT, ws, we);
      g_trProjPts[1].y = tr_y(100);
      char hm[12]; fmt_hm(etaT, hm, sizeof(hm));
      snprintf(b, sizeof(b), TRS("No ritmo atual, esgota as %s (em %dh%02dm)",
                                 "At this pace, runs out at %s (in %dh%02dm)"),
               hm, (int)minsLeft / 60, (int)minsLeft % 60);
      lv_label_set_text(g_ui.trCap, b);
      lv_obj_set_style_text_color(g_ui.trCap, lv_color_hex(minsLeft < 60 ? C_BAD : C_WARN), 0);
    } else {
      float endPct = g_usage.h5 + rate * ((we - (uint32_t)now) / 60.0f);
      g_trProjPts[1].x = tr_x(we, ws, we);
      g_trProjPts[1].y = tr_y(endPct);
      snprintf(b, sizeof(b), TRS("No ritmo atual, NAO esgota antes do reset (~%d%%)",
                                 "At this pace, does NOT run out before reset (~%d%%)"),
               (int)(endPct + 0.5f));
      lv_label_set_text(g_ui.trCap, b);
      lv_obj_set_style_text_color(g_ui.trCap, lv_color_hex(C_OK), 0);
    }
    lv_line_set_points(g_ui.trProj, g_trProjPts, 2);
  } else {
    lv_line_set_points(g_ui.trProj, g_trProjPts, 0);
    lv_label_set_text(g_ui.trCap, TRS("Uso estavel \xE2\x80\xA2 sem risco no momento",
                                      "Stable usage \xE2\x80\xA2 no risk right now"));
    lv_obj_set_style_text_color(g_ui.trCap, lv_color_hex(C_OK), 0);
  }
}

void heat_redraw() {
  if (!g_ui.heat[0]) return;
  float data[24];
  heat_mode_data(g_heatMode, data);
  float mx = 1.0f;
  for (int h = 0; h < 24; h++) if (data[h] > mx) mx = data[h];
  int curHour = -1; time_t now = time(nullptr);
  if (now > 1000000000L) { struct tm tv; localtime_r(&now, &tv); curHour = tv.tm_hour; }
  for (int h = 0; h < 24; h++) {
    if (!g_ui.heat[h]) continue;
    float r = data[h] / mx; if (r < 0) r = 0; if (r > 1) r = 1;
    int hgt = 4 + (int)(r * 114);
    lv_obj_set_size(g_ui.heat[h], 13, hgt);
    lv_obj_set_y(g_ui.heat[h], 176 - hgt);
    lv_obj_set_style_bg_color(g_ui.heat[h], lv_color_hex(h == curHour ? C_TEXT : C_ACCENT), 0);
    lv_obj_set_style_bg_opa(g_ui.heat[h], (lv_opa_t)(70 + (int)(r * 185)), 0);
  }
}

#include "ui_moments.h"
void refresh_ui_values() {
  if (g_state != ST_MAIN || !g_ui.agPct5) return;
  char b[96];

  // Agora: percentuais + medidores segmentados (cor desliza verde -> vermelho)
  // Arredondamento (int)(v + 0.5f) confirmado como a convencao correta: espelha
  // o painel oficial de Uso do Claude. Verificado em 23/07/2026 com truncamento,
  // que divergiu — bruto em [7.0,8.0) exibia 7 aqui e 8 no painel, provando que
  // o painel arredonda. Nao trocar por (int)v.
  //
  // Diferencas de 1 p.p. durante uso ativo NAO sao arredondamento: o dispositivo
  // faz poll a cada DEFAULT_POLL_SEC (120s) e fica ate um ciclo atras do painel,
  // que atualiza sob demanda. Comparar so com o dispositivo recem-atualizado.
  snprintf(b, sizeof(b), "%d%%", (int)(g_usage.h5 + 0.5f)); lv_label_set_text(g_ui.agPct5, b);
  lv_obj_set_style_text_color(g_ui.agPct5, grad_color(g_usage.h5), 0);
  set_meter(g_ui.seg5, g_usage.h5);
  snprintf(b, sizeof(b), "%d%%", (int)(g_usage.d7 + 0.5f)); lv_label_set_text(g_ui.agPct7, b);
  lv_obj_set_style_text_color(g_ui.agPct7, grad_color(g_usage.d7), 0);
  set_meter(g_ui.seg7, g_usage.d7);

  // ⚠️ A COR SAI DO NUMERO, nao do status textual.
  //
  // `statusOverall` vinha do cabecalho `unified-status` da Anthropic. Desde que
  // a fonte passou a ser a VPS (25/08), o payload NAO carrega esse campo e o
  // mapeamento o preenche com "allowed" fixo — entao status_color() devolvia
  // C_OK sempre, e o chip ficava VERDE a 90%. Nao quebrava, nao logava: so
  // parava de avisar, que e o pior modo de falha num aparelho cuja funcao e
  // justamente avisar.
  //
  // A janela que manda e a MAIS severa das duas: e a que restringe de verdade.
  {
    float pior = g_usage.h5 > g_usage.d7 ? g_usage.h5 : g_usage.d7;
    set_chip(g_ui.agChip, overall_label(g_usage.statusOverall),
             cota_sev_cor(pior));
  }
  update_tok_row();

  // Modelos: chips de sonda + incidente
  for (int i = 0; i < NMODELS; i++) {
    if (!g_ui.mChip[i]) continue;
    char txt[16]; uint32_t col;
    model_chip(i, txt, sizeof(txt), &col);
    set_chip(g_ui.mChip[i], txt, col);
    lv_obj_update_layout(g_ui.mChip[i]);
    static const int CENTERS[NMODELS] = {60, 180, 300, 420};
    lv_obj_set_x(g_ui.mChip[i], CENTERS[i] - lv_obj_get_width(g_ui.mChip[i]) / 2);
  }
  if (g_ui.incident) {
    bool any = !(g_status.haikuUp && g_status.sonnetUp && g_status.opusUp && g_status.fableUp);
    lv_label_set_text(g_ui.incident,
        !g_status.ok ? TRS("status.claude.com: sem dados", "status.claude.com: no data")
        : (any ? TRS("Incidente ativo \xE2\x80\xA2 veja status.claude.com",
                     "Active incident \xE2\x80\xA2 see status.claude.com")
               : TRS("status.claude.com: OK \xE2\x80\xA2 sem incidentes",
                     "status.claude.com: OK \xE2\x80\xA2 no incidents")));
    lv_obj_set_style_text_color(g_ui.incident, lv_color_hex(any ? C_WARN : C_FAINT), 0);
  }

  trend_redraw();
  heat_redraw();
  dash_tick();
  refresh_codex_values();
  refresh_github();
}

static void cx_win(lv_obj_t *pct, lv_obj_t **seg, bool has, float v) {
  char b[16];
  if (has) {
    snprintf(b, sizeof(b), "%d%%", (int)(v + 0.5f));
    lv_label_set_text(pct, b);
    lv_obj_set_style_text_color(pct, grad_color(v), 0);
    set_meter(seg, v);
  } else {
    // Janela 5h null = sem uso nas ultimas 5h = 0% usado (nao "sem dado").
    lv_label_set_text(pct, "0%");
    lv_obj_set_style_text_color(pct, grad_color(0), 0);
    set_meter(seg, 0);
  }
}

// Milhar com separador local (1697 -> "1.697" em pt, "1,697" em en).
static void fmt_thousand(uint32_t v, char *out, size_t sz) {
  char raw[16]; snprintf(raw, sizeof(raw), "%u", (unsigned)v);
  int len = strlen(raw), o = 0; char sepc = g_lang ? ',' : '.';
  for (int i = 0; i < len && o < (int)sz - 1; i++) {
    if (i > 0 && (len - i) % 3 == 0 && o < (int)sz - 1) out[o++] = sepc;
    out[o++] = raw[i];
  }
  out[o] = 0;
}

// Cor canônica por origem — a MESMA nas barras de Origem e nos segmentos do gráfico
// diário, para a tela Origem funcionar como legenda de cores das duas.
static uint32_t surf_color(const char *k) {
  if (!strcmp(k, "cli"))          return 0xEC4899;  // rosa
  if (!strcmp(k, "web"))          return 0x4B6BFF;  // azul (Codex)
  if (!strcmp(k, "github"))       return 0x4ADE80;  // verde
  if (!strcmp(k, "desktop"))      return 0xF97316;  // laranja
  if (!strcmp(k, "desktop_work")) return 0xEF4444;  // vermelho
  return C_MUTED;
}
static const char *surf_short(const char *k) {  // legenda cabe melhor
  return !strcmp(k, "desktop_work") ? "dwork" : k;
}

// Preenche uma lista rankeada (Origem/Modelo): label + barra proporcional ao % + "NN%".
// colorSurf=true (Origem) pinta cada barra com a cor da origem; false (Modelo) usa azul Codex.
static void fill_rank(lv_obj_t **lbl, lv_obj_t **bar, lv_obj_t **val,
                      CxAnItem *items, uint8_t n, bool colorSurf) {
  for (int i = 0; i < CXAN_ROWS; i++) {
    if (!lbl[i] || !bar[i] || !val[i]) continue;
    if (i < n && items[i].key[0]) {
      lv_label_set_text(lbl[i], items[i].key);
      int w = (int)(items[i].pct / 100.0f * RANK_BARW);
      if (w < 3) w = 3; if (w > RANK_BARW) w = RANK_BARW;
      lv_obj_set_width(bar[i], w);
      if (colorSurf) {
        lv_obj_set_style_bg_color(bar[i], lv_color_hex(surf_color(items[i].key)), 0);
        lv_obj_set_style_bg_opa(bar[i], LV_OPA_COVER, 0);
      } else {
        lv_obj_set_style_bg_color(bar[i], lv_color_hex(C_CODEX), 0);
        lv_obj_set_style_bg_opa(bar[i], (lv_opa_t)(120 + (int)(items[i].pct * 1.35f)), 0);
      }
      char b[16]; snprintf(b, sizeof(b), "%u%%", items[i].pct);
      lv_label_set_text(val[i], b);
    } else {
      lv_label_set_text(lbl[i], "");
      lv_label_set_text(val[i], "");
      lv_obj_set_width(bar[i], 0);
    }
  }
}

// Telas novas do Codex: Origem, Modelo, Interações (dados da seção "an" do bridge).
void refresh_codex_analytics() {
  if (!g_ui.cxOrigLbl[0]) return;
  bool has = g_codex.hasAn;
  char cap[80];

  fill_rank(g_ui.cxOrigLbl, g_ui.cxOrigBar, g_ui.cxOrigVal,
            g_codex.surface, has ? g_codex.nSurface : 0, true);   // Origem: cor por origem
  fill_rank(g_ui.cxMdlLbl, g_ui.cxMdlBar, g_ui.cxMdlVal,
            g_codex.model, has ? g_codex.nModel : 0, false);      // Modelo: azul Codex

  // Fonte embarcada sem glifos acentuados/travessão → strings em ASCII (ver build_tile_codex_inter).
  if (g_ui.cxOrigCap) {
    if (has) { char t[16]; fmt_thousand((uint32_t)(g_codex.creditsTotal + 0.5f), t, sizeof(t));
      snprintf(cap, sizeof(cap), TRS("%s creditos \xE2\x80\xA2 %ud", "%s credits \xE2\x80\xA2 %ud"),
               t, g_codex.anRangeDays); }
    else strlcpy(cap, TRS("Coletando dados...", "Collecting data..."), sizeof(cap));
    lv_label_set_text(g_ui.cxOrigCap, cap);
  }
  if (g_ui.cxMdlCap) {
    if (has) { char t[16]; fmt_thousand(g_codex.interactions, t, sizeof(t));
      snprintf(cap, sizeof(cap), TRS("%s interacoes \xE2\x80\xA2 %ud", "%s interactions \xE2\x80\xA2 %ud"),
               t, g_codex.anRangeDays); }
    else strlcpy(cap, TRS("Coletando dados...", "Collecting data..."), sizeof(cap));
    lv_label_set_text(g_ui.cxMdlCap, cap);
  }

  // Interações: número grande + subtítulo + mini-gráfico diário
  if (g_ui.cxIntBig) {
    char big[16]; fmt_thousand(has ? g_codex.interactions : 0, big, sizeof(big));
    lv_label_set_text(g_ui.cxIntBig, has ? big : "--");
    if (has) snprintf(cap, sizeof(cap), TRS("%u conversas \xE2\x80\xA2 %ud", "%u threads \xE2\x80\xA2 %ud"),
                      (unsigned)g_codex.anThreads, g_codex.anRangeDays);
    else strlcpy(cap, TRS("aguardando o bridge", "waiting for bridge"), sizeof(cap));
    lv_label_set_text(g_ui.cxIntSub, cap);
  }
  if (g_ui.cxDaySeg[0][0]) {
    uint16_t mx = 1;                       // escala pela maior coluna (total do dia)
    for (int i = 0; i < g_codex.nDay; i++) if (g_codex.day[i].credits > mx) mx = g_codex.day[i].credits;
    for (int i = 0; i < CXAN_DAYS; i++) {
      if (has && i < g_codex.nDay) {
        int yTop = CXDAY_BASE;             // empilha de baixo p/ cima, origem por origem
        for (int s = 0; s < CXAN_SURF; s++) {
          lv_obj_t *seg = g_ui.cxDaySeg[i][s];
          uint16_t v = (s < g_codex.nSurfOrder) ? g_codex.day[i].v[s] : 0;
          if (v == 0) { lv_obj_add_flag(seg, LV_OBJ_FLAG_HIDDEN); continue; }
          int h = (int)((float)v / mx * CXDAY_MAXH + 0.5f);
          if (h < 2) h = 2;
          yTop -= h;
          lv_obj_set_size(seg, 22, h);
          lv_obj_set_pos(seg, 14 + i * 32, yTop);
          lv_obj_set_style_bg_color(seg, lv_color_hex(surf_color(g_codex.surfOrder[s])), 0);
          lv_obj_clear_flag(seg, LV_OBJ_FLAG_HIDDEN);
        }
      } else {
        for (int s = 0; s < CXAN_SURF; s++) lv_obj_add_flag(g_ui.cxDaySeg[i][s], LV_OBJ_FLAG_HIDDEN);
      }
    }
    for (int s = 0; s < CXAN_SURF; s++) {   // legenda de cores
      if (has && s < g_codex.nSurfOrder) {
        lv_obj_set_style_bg_color(g_ui.cxLegDot[s], lv_color_hex(surf_color(g_codex.surfOrder[s])), 0);
        lv_obj_clear_flag(g_ui.cxLegDot[s], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_ui.cxLegLbl[s], surf_short(g_codex.surfOrder[s]));
      } else {
        lv_obj_add_flag(g_ui.cxLegDot[s], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(g_ui.cxLegLbl[s], "");
      }
    }
    if (g_ui.cxDayCap) {
      if (has && g_codex.nDay > 0)
        snprintf(cap, sizeof(cap), TRS("%s a %s", "%s to %s"),
                 g_codex.day[0].label, g_codex.day[g_codex.nDay - 1].label);
      else strlcpy(cap, TRS("sem historico ainda", "no history yet"), sizeof(cap));
      lv_label_set_text(g_ui.cxDayCap, cap);
    }
  }
}

// Card "7 DIAS" do tile Agora: total de créditos (7d) + barras diárias.
static void refresh_cx_7d_card() {
  if (!g_ui.cxD7Total) return;
  bool has = g_codex.hasAn && g_codex.nDay > 0;
  int start = g_codex.nDay > 7 ? g_codex.nDay - 7 : 0;
  uint32_t total = 0; uint16_t mx = 1;
  for (int i = start; i < g_codex.nDay; i++) {
    total += g_codex.day[i].credits;
    if (g_codex.day[i].credits > mx) mx = g_codex.day[i].credits;
  }
  if (has) { char b[16]; fmt_thousand(total, b, sizeof(b)); lv_label_set_text(g_ui.cxD7Total, b); }
  else lv_label_set_text(g_ui.cxD7Total, "--");
  for (int i = 0; i < 7; i++) {
    int di = start + i;
    if (has && di < g_codex.nDay) {
      int h = (int)((float)g_codex.day[di].credits / mx * CXD7_MAXH + 0.5f); if (h < 3) h = 3;
      lv_obj_set_size(g_ui.cxD7Bar[i], 20, h);
      lv_obj_set_y(g_ui.cxD7Bar[i], CXD7_BASE - h);
      lv_obj_clear_flag(g_ui.cxD7Bar[i], LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(g_ui.cxD7Lbl[i], g_codex.day[di].label + 3);   // "DD" de "MM-DD"
    } else {
      lv_obj_add_flag(g_ui.cxD7Bar[i], LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(g_ui.cxD7Lbl[i], "");
    }
  }
}

void refresh_codex_values() {
  if (g_state != ST_MAIN || !g_ui.cxPct7) return;
  refresh_cx_7d_card();
  cx_win(g_ui.cxPct7, g_ui.cxSeg7, g_codex.has7d, g_codex.pct7);
  if (g_ui.cxChip) {
    if (!g_codex.ok || g_codex.stale) set_chip(g_ui.cxChip, TRS("DADO VELHO","STALE"), C_WARN);
    else set_chip(g_ui.cxChip, g_codex.limitReached ? TRS("BLOQUEADO","BLOCKED") : "OK",
                  g_codex.limitReached ? C_BAD : C_OK);
  }
  cx_trend_redraw();
  refresh_codex_analytics();
}

// Atualiza o texto de status do cabeçalho (sem trocar de tela)
void set_hdr_status() {
  if (!g_hdrStatus) return;
  char buf[40]; uint32_t color;
  if (g_refreshing)        { strcpy(buf, TRS("atualizando...", "updating..."));      color = C_ACCENT; }
  else if (!g_lastFetchOk) { strcpy(buf, TRS("falha ao atualizar", "update failed")); color = C_BAD; }
  else {
    uint32_t s = (millis() - g_lastOkMs) / 1000;
    if (s < 60) snprintf(buf, sizeof(buf), TRS("atualizado ha %us", "updated %us ago"), (unsigned)s);
    else        snprintf(buf, sizeof(buf), TRS("atualizado ha %umin", "updated %um ago"), (unsigned)(s / 60));
    color = C_MUTED;
  }
  lv_label_set_text(g_hdrStatus, buf);
  lv_obj_set_style_text_color(g_hdrStatus, lv_color_hex(color), 0);
}
// Botão de refresh: só pede; a busca acontece em background no loop()
