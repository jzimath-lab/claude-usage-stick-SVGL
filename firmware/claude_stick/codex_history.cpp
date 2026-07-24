#include "codex_history.h"
#include "ui_helpers.h"
#include <LittleFS.h>
#include <time.h>

// tr_y (percent->y) reusado do modulo de tiles; x calculado inline p/ a janela 7d.
extern int tr_y(float p);
#define CTR_X0 12
#define CTR_W  440
#define CTR_H  126

// ---- Estado (file-local) ----
static CxSample g_cxHist[CX_HIST_MAX];
static int g_cxHistN = 0, g_cxHistHead = 0;
static float g_cxHourBurn[24] = {0};
static float g_cxLastPct = -1.0f;
static lv_point_precise_t g_cxTrPts[CX_HIST_MAX];
static lv_point_precise_t g_cxTrProjPts[2];

static int cx_hist_idx(int i) { return (g_cxHistHead - g_cxHistN + i + CX_HIST_MAX * 2) % CX_HIST_MAX; }

void cx_hist_push(float pct7) {
  time_t now = time(nullptr);
  g_cxHist[g_cxHistHead].t   = (now > 1000000000L) ? (uint32_t)now : 0;
  g_cxHist[g_cxHistHead].pct = (uint8_t)(pct7 + 0.5f);
  g_cxHistHead = (g_cxHistHead + 1) % CX_HIST_MAX;
  if (g_cxHistN < CX_HIST_MAX) g_cxHistN++;
}

void cx_accum_heat(float pct7) {
  time_t now = time(nullptr);
  if (g_cxLastPct >= 0 && now > 1000000000L) {
    float d = pct7 - g_cxLastPct;
    if (d > 0 && d < 100) {
      struct tm tv; localtime_r(&now, &tv);
      g_cxHourBurn[tv.tm_hour] += d;
    }
  }
  g_cxLastPct = pct7;
}

// ---- Persistência (arquivo próprio; separado do /hist.bin do Claude) ----
#define CX_MAGIC 0x43585631u   // "CXV1"
struct CxFile { uint32_t magic; int n, head; CxSample hist[CX_HIST_MAX]; float hourBurn[24]; float lastPct; };

void cx_save_history() {
  File f = LittleFS.open("/cxhist.tmp", "w");
  if (!f) return;
  static CxFile cf;
  cf.magic = CX_MAGIC; cf.n = g_cxHistN; cf.head = g_cxHistHead;
  memcpy(cf.hist, g_cxHist, sizeof(g_cxHist));
  memcpy(cf.hourBurn, g_cxHourBurn, sizeof(g_cxHourBurn));
  cf.lastPct = g_cxLastPct;
  size_t w = f.write((uint8_t *)&cf, sizeof(cf));
  f.close();
  if (w != sizeof(cf)) { LittleFS.remove("/cxhist.tmp"); return; }
  LittleFS.remove("/cxhist.bin");
  LittleFS.rename("/cxhist.tmp", "/cxhist.bin");
}

void cx_load_history() {
  File f = LittleFS.open("/cxhist.bin", "r");
  if (!f) return;
  static CxFile cf;
  if (f.read((uint8_t *)&cf, sizeof(cf)) == (int)sizeof(cf) && cf.magic == CX_MAGIC) {
    g_cxHistN = cf.n; g_cxHistHead = cf.head;
    memcpy(g_cxHist, cf.hist, sizeof(g_cxHist));
    memcpy(g_cxHourBurn, cf.hourBurn, sizeof(g_cxHourBurn));
    g_cxLastPct = cf.lastPct;
  }
  f.close();
}

// ---- Redraw: Janela 7d (histórico + projeção) ----
static int ctr_x(uint32_t tt, uint32_t ws, uint32_t we) {
  if (we <= ws) return CTR_X0;
  long long v = (long long)(tt - ws) * CTR_W / (long long)(we - ws);
  if (v < 0) v = 0; if (v > CTR_W) v = CTR_W;
  return CTR_X0 + (int)v;
}

void cx_trend_redraw() {
  if (!g_ui.cxTrHist) return;
  time_t now = time(nullptr);
  uint32_t we = g_codex.reset7Epoch;
  bool clockOk = (now > 1000000000L) && we != 0 && g_codex.has7d;
  if (!clockOk) {
    lv_line_set_points(g_ui.cxTrHist, g_cxTrPts, 0);
    lv_line_set_points(g_ui.cxTrProj, g_cxTrProjPts, 0);
    lv_obj_add_flag(g_ui.cxTrDot, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(g_ui.cxTrCap, TRS("Aguardando dados da semana...", "Waiting for week data..."));
    lv_obj_set_style_text_color(g_ui.cxTrCap, lv_color_hex(C_MUTED), 0);
    return;
  }
  uint32_t ws = we - 7u * 24 * 3600;
  char t0[12], t1[12], b[112];
  fmt_hm(ws, t0, sizeof(t0)); fmt_hm(we, t1, sizeof(t1));
  lv_label_set_text(g_ui.cxTrT0, t0);
  lv_label_set_text(g_ui.cxTrT1, t1);

  int n = 0;
  for (int i = 0; i < g_cxHistN && n < CX_HIST_MAX; i++) {
    CxSample s = g_cxHist[cx_hist_idx(i)];
    if (s.t == 0 || s.t < ws || s.t > (uint32_t)now) continue;
    g_cxTrPts[n].x = ctr_x(s.t, ws, we);
    g_cxTrPts[n].y = tr_y(s.pct);
    n++;
  }
  uint32_t nowC = ((uint32_t)now > we) ? we : (uint32_t)now;
  if (n < CX_HIST_MAX) {
    g_cxTrPts[n].x = ctr_x(nowC, ws, we);
    g_cxTrPts[n].y = tr_y(g_codex.pct7);
    n++;
  }
  lv_line_set_points(g_ui.cxTrHist, g_cxTrPts, n);
  int cx = ctr_x(nowC, ws, we), cy = tr_y(g_codex.pct7);
  lv_obj_set_pos(g_ui.cxTrDot, cx - 4, cy - 4);
  lv_obj_clear_flag(g_ui.cxTrDot, LV_OBJ_FLAG_HIDDEN);

  if (n < 3) {
    lv_line_set_points(g_ui.cxTrProj, g_cxTrProjPts, 0);
    lv_label_set_text(g_ui.cxTrCap, TRS("Coletando dados... (enche em dias)",
                                        "Collecting data... (fills over days)"));
    lv_obj_set_style_text_color(g_ui.cxTrCap, lv_color_hex(C_MUTED), 0);
    return;
  }
  // taxa: janela das últimas ~6h
  float rate = 0;   // %/h
  {
    CxSample first = {0, 0};
    for (int i = 0; i < g_cxHistN; i++) {
      CxSample s = g_cxHist[cx_hist_idx(i)];
      if (s.t == 0 || s.t < ws) continue;
      if (s.t >= (uint32_t)now - 6 * 3600) { first = s; break; }
    }
    if (first.t != 0 && (uint32_t)now > first.t + 1800) {
      float dh = ((uint32_t)now - first.t) / 3600.0f;
      rate = (g_codex.pct7 - first.pct) / dh;
    }
  }
  if (g_codex.pct7 >= 99.5f) {
    lv_line_set_points(g_ui.cxTrProj, g_cxTrProjPts, 0);
    lv_label_set_text(g_ui.cxTrCap, TRS("Semana esgotada", "Week exhausted"));
    lv_obj_set_style_text_color(g_ui.cxTrCap, lv_color_hex(C_BAD), 0);
  } else if (rate > 0.01f) {
    float hLeft = (100.0f - g_codex.pct7) / rate;
    uint32_t etaT = (uint32_t)now + (uint32_t)(hLeft * 3600);
    g_cxTrProjPts[0].x = cx; g_cxTrProjPts[0].y = cy;
    if (etaT <= we) {
      g_cxTrProjPts[1].x = ctr_x(etaT, ws, we);
      g_cxTrProjPts[1].y = tr_y(100);
      snprintf(b, sizeof(b), TRS("No ritmo, esgota em %dd%02dh",
                                 "At this pace, out in %dd%02dh"), (int)hLeft / 24, (int)hLeft % 24);
      lv_label_set_text(g_ui.cxTrCap, b);
      lv_obj_set_style_text_color(g_ui.cxTrCap, lv_color_hex(hLeft < 24 ? C_BAD : C_WARN), 0);
    } else {
      float endPct = g_codex.pct7 + rate * ((we - (uint32_t)now) / 3600.0f);
      g_cxTrProjPts[1].x = ctr_x(we, ws, we);
      g_cxTrProjPts[1].y = tr_y(endPct);
      snprintf(b, sizeof(b), TRS("No ritmo, NAO esgota antes do reset (~%d%%)",
                                 "At this pace, does NOT run out (~%d%%)"), (int)(endPct + 0.5f));
      lv_label_set_text(g_ui.cxTrCap, b);
      lv_obj_set_style_text_color(g_ui.cxTrCap, lv_color_hex(C_OK), 0);
    }
    lv_line_set_points(g_ui.cxTrProj, g_cxTrProjPts, 2);
  } else {
    lv_line_set_points(g_ui.cxTrProj, g_cxTrProjPts, 0);
    lv_label_set_text(g_ui.cxTrCap, TRS("Uso estavel \xE2\x80\xA2 sem risco", "Stable \xE2\x80\xA2 no risk"));
    lv_obj_set_style_text_color(g_ui.cxTrCap, lv_color_hex(C_OK), 0);
  }
}

// ---- Redraw: Ritmo por hora ----
void cx_heat_redraw() {
  if (!g_ui.cxHeat[0]) return;
  float mx = 1.0f;
  for (int h = 0; h < 24; h++) if (g_cxHourBurn[h] > mx) mx = g_cxHourBurn[h];
  int curHour = -1; time_t now = time(nullptr);
  if (now > 1000000000L) { struct tm tv; localtime_r(&now, &tv); curHour = tv.tm_hour; }
  for (int h = 0; h < 24; h++) {
    if (!g_ui.cxHeat[h]) continue;
    float r = g_cxHourBurn[h] / mx; if (r < 0) r = 0; if (r > 1) r = 1;
    int hgt = 4 + (int)(r * 114);
    lv_obj_set_size(g_ui.cxHeat[h], 13, hgt);
    lv_obj_set_y(g_ui.cxHeat[h], 176 - hgt);
    lv_obj_set_style_bg_color(g_ui.cxHeat[h], lv_color_hex(h == curHour ? C_TEXT : C_CODEX), 0);
    lv_obj_set_style_bg_opa(g_ui.cxHeat[h], (lv_opa_t)(70 + (int)(r * 185)), 0);
  }
}
