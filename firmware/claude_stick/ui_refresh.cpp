#include "ui_refresh.h"
#include "ui_dashboard.h"
#include "ui_moments.h"

// Ainda no claude_stick.ino; migram para ui_tiles.*
int tr_x(uint32_t tt, uint32_t ws, uint32_t we);
int tr_y(float p);
int hist_idx(int i);
void heat_mode_data(int mode, float out[24]);

// Atualização de valores
// ============================================================
void update_tok_row() {
  if (!g_ui.agTok) return;
  if (g_tok.atMs == 0 || millis() - g_tok.atMs > TOK_FRESH_MS) {
    lv_label_set_text(g_ui.agTok, "");
    return;
  }
  char a[16], b[16], s[96];
  fmt_tok(g_tok.tin, a, sizeof(a));
  fmt_tok(g_tok.tout, b, sizeof(b));
  snprintf(s, sizeof(s), TRS("tokens na janela: %s entrada \xE2\x80\xA2 %s saida",
                             "window tokens: %s in \xE2\x80\xA2 %s out"), a, b);
  lv_label_set_text(g_ui.agTok, s);
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

  set_chip(g_ui.agChip, overall_label(g_usage.statusOverall), status_color(g_usage.statusOverall));
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
