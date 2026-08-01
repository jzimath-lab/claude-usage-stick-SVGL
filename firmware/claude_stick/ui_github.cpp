// Deck GitHub (3o provider) — construcao E atualizacao das tres telas.
//
// Fica em arquivo proprio porque ui_refresh.cpp ja estava em 452 linhas e o
// gate do CI avisa acima de 500. Os helpers visuais vem de ui_dashboard.h,
// entao nao ha duplicacao.
//
// TRES JANELAS diferentes convivem aqui, e cada bloco DIZ a sua — a auditoria
// do bridge do Codex mostrou o custo de nao fazer isso (numero de 30 dias
// sobre grafico de 14, nenhum dos dois rotulado):
//   AGORA    -> ciclo de faturamento
//   PROJETOS -> 14 dias
//   JOBS     -> ciclo, com rodape de CI de 7 dias
//
// Todo texto em ASCII: a montserrat embarcada nao tem glifo acentuado.
#include "ui_tiles.h"
#include "ui_dashboard.h"
#include "ui_helpers.h"
#include "github_logo.h"

// Faixa empilhada por projeto no tile AGORA
#define FX_X 12
#define FX_Y 196
#define FX_W 452
#define FX_H 18

// Grafico diario do tile PROJETOS
#define GD_X0 14
#define GD_BASE 208
#define GD_H 84
#define GD_W 30
#define GD_BARW 22

// Cor de cada fatia/projeto. Por INDICE na ordem do servidor (proj_order), que
// e estavel dentro de um ciclo; a legenda usa a mesma tabela, entao barra e
// rotulo nunca discordam.
static uint32_t proj_color(uint8_t i) {
  static const uint32_t C[GH_PROJ] = { 0x58A6FF, 0xBC8CFF, 0x3FB950, 0xF0883E, 0x8B949E };
  return C[i % GH_PROJ];
}

// ---------------------------------------------------------------- AGORA ----

void build_tile_github(lv_obj_t *t) {
  // Card COTA GRATIS
  lv_obj_t *a = card(t, 8, 4, 228, 180);
  tstatic(a, "COTA GRATIS", &lv_font_montserrat_14, C_MUTED, 0, 0);
  g_ui.ghPct = tlabel(a, &lv_font_montserrat_48, C_TEXT, 0, 18);
  // Barra em duas fatias: gratis (verde) + pago (vermelho). Ela TRANSBORDA em
  // vez de saturar — o consumo medido deu 382% e um medidor que para em 100%
  // mentiria justamente no cenario que importa.
  rrect(a, 0, 92, 204, 16, 4, C_TRACK);
  g_ui.ghBarFree = rrect(a, 0, 92, 2, 16, 4, C_OK);
  g_ui.ghBarPaid = rrect(a, 2, 92, 2, 16, 4, C_BAD);
  g_ui.ghCotaSub = tlabel(a, &lv_font_montserrat_12, C_FAINT, 0, 116);
  lv_obj_set_width(g_ui.ghCotaSub, 204);

  // Card A PAGAR
  lv_obj_t *b = card(t, 244, 4, 228, 180);
  tstatic(b, "A PAGAR", &lv_font_montserrat_14, C_MUTED, 0, 0);
  g_ui.ghUsd = tlabel(b, &lv_font_montserrat_48, C_OK, 0, 18);
  g_ui.ghUsdSub = tlabel(b, &lv_font_montserrat_12, C_FAINT, 0, 116);
  lv_obj_set_width(g_ui.ghUsdSub, 204);
  for (int i = 0; i < 7; i++)
    g_ui.ghDia[i] = rrect(b, i * 29, 108, 22, 3, 2, C_GITHUB);   // altura no update

  // Faixa por projeto (rateio) — resumo, sem swipe
  tstatic(t, "POR PROJETO", &lv_font_montserrat_12, C_MUTED, FX_X, FX_Y - 16);
  rrect(t, FX_X, FX_Y, FX_W, FX_H, 4, C_TRACK);
  for (int i = 0; i < GH_PROJ; i++)
    g_ui.ghFaixa[i] = rrect(t, FX_X, FX_Y, 2, FX_H, 0, proj_color(i));
  g_ui.ghFxLbl = tlabel(t, &lv_font_montserrat_12, C_FAINT, FX_X, FX_Y + 22);
  lv_obj_set_width(g_ui.ghFxLbl, FX_W);
}

// ------------------------------------------------------------- PROJETOS ----

void build_tile_github_proj(lv_obj_t *t) {
  tstatic(t, "Projetos", &lv_font_montserrat_16, C_TEXT, 14, 4);
  tstatic(t, "ciclo", &lv_font_montserrat_12, C_FAINT, 430, 8);

  lv_obj_t *c = card(t, 8, 26, 464, 120);
  lv_obj_set_style_pad_all(c, 12, 0);
  for (int i = 0; i < GH_ROWS; i++) {
    int y = 2 + i * 20;
    g_ui.ghProjLbl[i] = tlabel(c, &lv_font_montserrat_14, C_TEXT, 0, y);
    lv_obj_set_width(g_ui.ghProjLbl[i], RANK_BARX - 8);
    rrect(c, RANK_BARX, y + 4, RANK_BARW, 9, 4, C_TRACK);
    g_ui.ghProjBar[i] = rrect(c, RANK_BARX, y + 4, 2, 9, 4, C_GITHUB);
    g_ui.ghProjVal[i] = tlabel(c, &lv_font_montserrat_14, C_MUTED,
                               RANK_BARX + RANK_BARW + 8, y);
    lv_obj_set_width(g_ui.ghProjVal[i], 88);
  }

  tstatic(t, "MINUTOS POR DIA - 14 DIAS", &lv_font_montserrat_12, C_MUTED, 14, 152);
  for (int d = 0; d < GH_DAYS; d++)
    for (int k = 0; k < GH_PROJ; k++)
      g_ui.ghDaySeg[d][k] = rrect(t, GD_X0 + d * GD_W, GD_BASE, GD_BARW, 2, 0,
                                  proj_color(k));
  g_ui.ghDayCap = tlabel(t, &lv_font_montserrat_12, C_FAINT, 14, GD_BASE + 8);
  lv_obj_set_width(g_ui.ghDayCap, 452);
}

// ----------------------------------------------------------------- JOBS ----

void build_tile_github_jobs(lv_obj_t *t) {
  tstatic(t, "Jobs", &lv_font_montserrat_16, C_TEXT, 14, 4);
  tstatic(t, "ciclo", &lv_font_montserrat_12, C_FAINT, 430, 8);

  lv_obj_t *c = card(t, 8, 26, 464, 120);
  lv_obj_set_style_pad_all(c, 12, 0);
  for (int i = 0; i < GH_ROWS; i++) {
    int y = 2 + i * 20;
    g_ui.ghJobLbl[i] = tlabel(c, &lv_font_montserrat_14, C_TEXT, 0, y);
    lv_obj_set_width(g_ui.ghJobLbl[i], RANK_BARX - 8);
    rrect(c, RANK_BARX, y + 4, RANK_BARW, 9, 4, C_TRACK);
    g_ui.ghJobBar[i] = rrect(c, RANK_BARX, y + 4, 2, 9, 4, C_GITHUB);
    g_ui.ghJobVal[i] = tlabel(c, &lv_font_montserrat_14, C_MUTED,
                              RANK_BARX + RANK_BARW + 8, y);
    lv_obj_set_width(g_ui.ghJobVal[i], 88);
  }

  // Rodape: minuto gasto em run que falhou e minuto pago duas vezes, porque o
  // push seguinte vai rodar tudo de novo.
  lv_obj_t *f = card(t, 8, 154, 464, 76);
  lv_obj_set_style_pad_all(f, 12, 0);
  tstatic(f, "CI VERMELHO - 7 DIAS", &lv_font_montserrat_12, C_MUTED, 0, 0);
  g_ui.ghCiPct = tlabel(f, &lv_font_montserrat_28, C_TEXT, 0, 16);
  g_ui.ghCiSub = tlabel(f, &lv_font_montserrat_12, C_FAINT, 0, 48);
  lv_obj_set_width(g_ui.ghCiSub, 200);
  g_ui.ghCiPerd = tlabel(f, &lv_font_montserrat_14, C_MUTED, 210, 20);
  lv_obj_set_width(g_ui.ghCiPerd, 230);
  lv_obj_set_style_text_align(g_ui.ghCiPerd, LV_TEXT_ALIGN_RIGHT, 0);
}

// ------------------------------------------------------------- REFRESH -----

static void set_bar(lv_obj_t *o, int w) {
  if (!o) return;
  if (w < 0) w = 0;
  lv_obj_set_width(o, w < 2 ? 2 : w);
}

void refresh_github(void) {
  const GithubUsage &g = g_github;
  char buf[64];

  // ---- AGORA ----
  if (g_ui.ghPct) {
    snprintf(buf, sizeof(buf), g.ok ? "%u%%" : "--", g.pct);
    lv_label_set_text(g_ui.ghPct, buf);
    lv_obj_set_style_text_color(g_ui.ghPct,
      lv_color_hex(g.pagosMin ? C_BAD : C_OK), 0);
  }
  if (g_ui.ghBarFree && g_ui.ghBarPaid) {
    // Proporcao dentro dos 204 px: a parte gratuita nunca passa do teto.
    uint32_t tot = g.usadosMin ? g.usadosMin : 1;
    uint32_t livre = g.usadosMin > g.pagosMin ? g.usadosMin - g.pagosMin : 0;
    int wFree = (int)((uint64_t)livre * 204 / tot);
    int wPaid = 204 - wFree;
    set_bar(g_ui.ghBarFree, g.usadosMin ? wFree : 0);
    lv_obj_set_pos(g_ui.ghBarPaid, wFree, 92);
    if (g.pagosMin) { set_bar(g_ui.ghBarPaid, wPaid);
                      lv_obj_clear_flag(g_ui.ghBarPaid, LV_OBJ_FLAG_HIDDEN); }
    else            { lv_obj_add_flag(g_ui.ghBarPaid, LV_OBJ_FLAG_HIDDEN); }
  }
  if (g_ui.ghCotaSub) {
    if (!g.ok) lv_label_set_text(g_ui.ghCotaSub, "sem dados");
    else if (g.pagosMin)
      snprintf(buf, sizeof(buf), "%u MIN - %u GRATIS", g.usadosMin, g.incluidosMin),
      lv_label_set_text(g_ui.ghCotaSub, buf);
    else
      snprintf(buf, sizeof(buf), "%u / %u MIN", g.usadosMin, g.incluidosMin),
      lv_label_set_text(g_ui.ghCotaSub, buf);
  }
  if (g_ui.ghUsd) {
    // Verde em zero, vermelho a partir do primeiro centavo: o painel so ganha
    // vermelho quando ha dinheiro real envolvido.
    snprintf(buf, sizeof(buf), g.ok ? "$%.2f" : "--", (double)g.usd);
    lv_label_set_text(g_ui.ghUsd, buf);
    lv_obj_set_style_text_color(g_ui.ghUsd,
      lv_color_hex(g.usd > 0.0f ? C_BAD : C_OK), 0);
  }
  if (g_ui.ghUsdSub) {
    if (!g.ok) lv_label_set_text(g_ui.ghUsdSub, "");
    else if (g.limiteUsd > 0)
      snprintf(buf, sizeof(buf), "%u%% DO LIMITE - %ud P/ FECHAR",
               g.pctLimite, g.diasRestantes),
      lv_label_set_text(g_ui.ghUsdSub, buf);
    else
      snprintf(buf, sizeof(buf), "%ud PARA FECHAR O CICLO", g.diasRestantes),
      lv_label_set_text(g_ui.ghUsdSub, buf);
  }
  // 7 ultimas barras diarias no card A PAGAR
  uint32_t maxDia = 1;
  for (uint8_t i = 0; i < g.nDay; i++) if (g.day[i].min > maxDia) maxDia = g.day[i].min;
  for (int i = 0; i < 7; i++) {
    if (!g_ui.ghDia[i]) continue;
    int idx = (int)g.nDay - 7 + i;
    if (idx < 0 || !g.ok) { lv_obj_add_flag(g_ui.ghDia[i], LV_OBJ_FLAG_HIDDEN); continue; }
    int h = (int)((uint64_t)g.day[idx].min * 46 / maxDia);
    if (h < 2) h = 2;
    lv_obj_set_size(g_ui.ghDia[i], 22, h);
    lv_obj_set_pos(g_ui.ghDia[i], i * 29, 154 - h);
    lv_obj_clear_flag(g_ui.ghDia[i], LV_OBJ_FLAG_HIDDEN);
  }
  // Faixa por projeto
  int x = FX_X;
  for (uint8_t i = 0; i < GH_PROJ; i++) {
    if (!g_ui.ghFaixa[i]) continue;
    if (i >= g.nProj || !g.ok || !g.usadosMin) {
      lv_obj_add_flag(g_ui.ghFaixa[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    int w = (int)((uint64_t)g.proj[i].min * FX_W / (g.usadosMin ? g.usadosMin : 1));
    if (w < 2) w = 2;
    lv_obj_set_size(g_ui.ghFaixa[i], w, FX_H);
    lv_obj_set_pos(g_ui.ghFaixa[i], x, FX_Y);
    lv_obj_clear_flag(g_ui.ghFaixa[i], LV_OBJ_FLAG_HIDDEN);
    x += w;
  }
  if (g_ui.ghFxLbl) {
    if (!g.ok || !g.nProj) lv_label_set_text(g_ui.ghFxLbl, "");
    else {
      // "rateio", nao "custo": a cota gratuita e da conta, nao do repo.
      snprintf(buf, sizeof(buf), "%s %u%% - %s %u%% (rateio)",
               g.proj[0].key, g.proj[0].pct,
               g.nProj > 1 ? g.proj[1].key : "", g.nProj > 1 ? g.proj[1].pct : 0);
      lv_label_set_text(g_ui.ghFxLbl, buf);
    }
  }

  // ---- PROJETOS ----
  for (int i = 0; i < GH_ROWS; i++) {
    if (!g_ui.ghProjLbl[i]) continue;
    if (i < g.nProj && g.ok) {
      lv_label_set_text(g_ui.ghProjLbl[i], g.proj[i].key);
      lv_obj_set_style_bg_color(g_ui.ghProjBar[i], lv_color_hex(proj_color(i)), 0);
      set_bar(g_ui.ghProjBar[i], g.proj[i].pct * RANK_BARW / 100);
      snprintf(buf, sizeof(buf), "%u  %u%%", g.proj[i].min, g.proj[i].pct);
      lv_label_set_text(g_ui.ghProjVal[i], buf);
    } else {
      lv_label_set_text(g_ui.ghProjLbl[i], "");
      lv_label_set_text(g_ui.ghProjVal[i], "");
      set_bar(g_ui.ghProjBar[i], 0);
    }
  }
  for (int d = 0; d < GH_DAYS; d++) {
    int idx = (int)g.nDay - GH_DAYS + d;
    int yTop = GD_BASE;
    for (int k = 0; k < GH_PROJ; k++) {
      lv_obj_t *s = g_ui.ghDaySeg[d][k];
      if (!s) continue;
      if (idx < 0 || !g.ok || k >= g.nProjOrder || !g.day[idx].v[k]) {
        lv_obj_add_flag(s, LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      int h = (int)((uint64_t)g.day[idx].v[k] * GD_H / (maxDia ? maxDia : 1));
      if (h < 2) h = 2;
      yTop -= h;
      lv_obj_set_size(s, GD_BARW, h);
      lv_obj_set_pos(s, GD_X0 + d * GD_W, yTop);
      lv_obj_clear_flag(s, LV_OBJ_FLAG_HIDDEN);
    }
  }
  if (g_ui.ghDayCap) {
    if (!g.ok || !g.nDay) lv_label_set_text(g_ui.ghDayCap, "coletando dados...");
    else {
      snprintf(buf, sizeof(buf), "%s a %s", g.day[0].label, g.day[g.nDay - 1].label);
      lv_label_set_text(g_ui.ghDayCap, buf);
    }
  }

  // ---- JOBS ----
  for (int i = 0; i < GH_ROWS; i++) {
    if (!g_ui.ghJobLbl[i]) continue;
    if (i < g.nJob && g.ok) {
      lv_label_set_text(g_ui.ghJobLbl[i], g.job[i].key);
      set_bar(g_ui.ghJobBar[i], g.job[i].pct * RANK_BARW / 100);
      snprintf(buf, sizeof(buf), "%u  %u%%", g.job[i].min, g.job[i].pct);
      lv_label_set_text(g_ui.ghJobVal[i], buf);
    } else {
      lv_label_set_text(g_ui.ghJobLbl[i], "");
      lv_label_set_text(g_ui.ghJobVal[i], "");
      set_bar(g_ui.ghJobBar[i], 0);
    }
  }
  if (g_ui.ghCiPct) {
    if (!g.ok || !g.hasCi) { lv_label_set_text(g_ui.ghCiPct, "--");
                             lv_label_set_text(g_ui.ghCiSub, "sem dados de CI");
                             lv_label_set_text(g_ui.ghCiPerd, ""); }
    else {
      snprintf(buf, sizeof(buf), "%u%%", g.ciPctFalha);
      lv_label_set_text(g_ui.ghCiPct, buf);
      lv_obj_set_style_text_color(g_ui.ghCiPct,
        lv_color_hex(g.ciPctFalha > 30 ? C_BAD : C_OK), 0);
      snprintf(buf, sizeof(buf), "%s - %u DE %u RUNS",
               g.ciRepo, g.ciFalhas, g.ciRuns);
      lv_label_set_text(g_ui.ghCiSub, buf);
      snprintf(buf, sizeof(buf), "%u MIN QUEIMADOS\nEM RUN VERMELHO", g.ciMinPerdidos);
      lv_label_set_text(g_ui.ghCiPerd, buf);
    }
  }
}
