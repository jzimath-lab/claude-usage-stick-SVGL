// Deck GitHub (3o provider) — construcao E atualizacao das tres telas.
//
// Fica em arquivo proprio porque ui_refresh.cpp ja estava em 452 linhas e o
// gate do CI avisa acima de 500. Os helpers visuais vem de ui_dashboard.h.
//
// TRES JANELAS diferentes convivem aqui, e cada bloco DIZ a sua — a auditoria
// do bridge do Codex mostrou o custo de nao fazer isso:
//   AGORA    -> ciclo de faturamento
//   PROJETOS -> 14 dias
//   JOBS     -> ciclo, com rodape de CI de 7 dias
//
// Todo texto em ASCII: a montserrat embarcada nao tem glifo acentuado.
#include "ui_tiles.h"
#include "ui_dashboard.h"
#include "ui_helpers.h"
#include "github_logo.h"

// --- Geometria das linhas rankeadas -----------------------------------------
// Barra mais ESTREITA que a do Codex (RANK_BARW=240) para sobrar rotulo: nomes
// como "Totvs-Moda-CRM" e "claude-usage-stick-SVGL" quebravam em duas linhas e
// invadiam a linha de baixo. Com largura fixa + LV_LABEL_LONG_DOT eles cortam
// com reticencias em UMA linha, e a leitura continua honesta.
#define GH_LBLW  138
#define GH_BARX  150
#define GH_BARW  170
#define GH_ROWH  18
#define GH_CARDY 22
#define GH_CARDH 104

// --- Grafico diario ---------------------------------------------------------
// Base subiu e altura diminuiu: antes o topo da coluna mais alta chegava a
// y=124 e passava POR CIMA do rotulo "MINUTOS POR DIA", em y=152.
#define GD_CAPY  126           // rotulo do grafico
#define GD_TOP   142           // topo maximo das colunas
#define GD_BASE  204           // linha de base
#define GD_H     (GD_BASE - GD_TOP)
#define GD_X0    16
#define GD_W     31            // passo entre colunas
#define GD_BARW  17            // largura da coluna (era 22 — pediram mais fina)
#define GD_LEGY  212           // legenda de cores

// Cor de cada fatia/projeto, por INDICE na ordem do servidor (proj_order), que
// e estavel dentro de um ciclo. A MESMA tabela pinta a legenda, entao barra e
// rotulo nunca discordam. Sem azul: ele e do Codex.
static uint32_t proj_color(uint8_t i) {
  static const uint32_t C[GH_PROJ] = { 0xE6EDF3, 0xBC8CFF, 0x3FB950, 0xF0883E, 0x8B949E };
  return C[i % GH_PROJ];
}

// Uma linha de ranking. Devolve o trilho para poder ESCONDER a linha inteira
// quando nao ha item — antes o trilho ficava visivel e a tela mostrava cinco
// barras com tres projetos.
static void build_rank_row(lv_obj_t *c, int y, lv_obj_t **lbl, lv_obj_t **trk,
                           lv_obj_t **bar, lv_obj_t **val) {
  *lbl = tlabel(c, &lv_font_montserrat_14, C_TEXT, 0, y);
  lv_obj_set_width(*lbl, GH_LBLW);
  lv_label_set_long_mode(*lbl, LV_LABEL_LONG_DOT);   // uma linha, corta com "..."
  *trk = rrect(c, GH_BARX, y + 4, GH_BARW, 9, 4, C_TRACK);
  *bar = rrect(c, GH_BARX, y + 4, 2, 9, 4, C_GITHUB);
  *val = tlabel(c, &lv_font_montserrat_14, C_MUTED, GH_BARX + GH_BARW + 10, y);
  lv_obj_set_width(*val, 96);
}

static void fill_rank_row(lv_obj_t *lbl, lv_obj_t *trk, lv_obj_t *bar, lv_obj_t *val,
                          bool tem, const char *nome, uint32_t min, uint8_t pct,
                          uint32_t cor) {
  auto vis = [](lv_obj_t *o, bool m) { if (!o) return;
    if (m) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); };
  vis(lbl, tem); vis(trk, tem); vis(bar, tem); vis(val, tem);
  if (!tem) return;
  char b[40];
  lv_label_set_text(lbl, nome);
  lv_obj_set_style_bg_color(bar, lv_color_hex(cor), 0);
  int w = (int)pct * GH_BARW / 100;
  lv_obj_set_width(bar, w < 3 ? 3 : w);
  snprintf(b, sizeof(b), "%u  %u%%", min, pct);
  lv_label_set_text(val, b);
}

// ---------------------------------------------------------------- AGORA ----

void build_tile_github(lv_obj_t *t) {
  lv_obj_t *a = card(t, 8, 4, 228, 180);
  tstatic(a, "COTA GRATIS", &lv_font_montserrat_14, C_MUTED, 0, 0);
  g_ui.ghPct = tlabel(a, &lv_font_montserrat_48, C_TEXT, 0, 18);
  // Barra em duas fatias: gratis (verde) + pago (vermelho). Ela TRANSBORDA em
  // vez de saturar — o consumo de julho deu 382%, e um medidor que para em
  // 100% mentiria justamente no cenario que importa.
  rrect(a, 0, 92, 204, 16, 4, C_TRACK);
  g_ui.ghBarFree = rrect(a, 0, 92, 2, 16, 4, C_OK);
  g_ui.ghBarPaid = rrect(a, 2, 92, 2, 16, 4, C_BAD);
  g_ui.ghCotaSub = tlabel(a, &lv_font_montserrat_12, C_FAINT, 0, 116);
  lv_obj_set_width(g_ui.ghCotaSub, 204);

  lv_obj_t *b = card(t, 244, 4, 228, 180);
  tstatic(b, "A PAGAR", &lv_font_montserrat_14, C_MUTED, 0, 0);
  g_ui.ghUsd = tlabel(b, &lv_font_montserrat_48, C_OK, 0, 18);
  g_ui.ghUsdSub = tlabel(b, &lv_font_montserrat_12, C_FAINT, 0, 116);
  lv_obj_set_width(g_ui.ghUsdSub, 204);
  for (int i = 0; i < 7; i++)
    g_ui.ghDia[i] = rrect(b, i * 29, 108, 20, 3, 2, C_GITHUB);

  tstatic(t, "POR PROJETO - RATEIO", &lv_font_montserrat_12, C_MUTED, 12, 188);
  rrect(t, 12, 204, 452, 16, 4, C_TRACK);
  for (int i = 0; i < GH_PROJ; i++)
    g_ui.ghFaixa[i] = rrect(t, 12, 204, 2, 16, 0, proj_color(i));
  g_ui.ghFxLbl = tlabel(t, &lv_font_montserrat_12, C_FAINT, 12, 224);
  lv_obj_set_width(g_ui.ghFxLbl, 452);
}

// ------------------------------------------------------------- PROJETOS ----

void build_tile_github_proj(lv_obj_t *t) {
  tstatic(t, "Projetos", &lv_font_montserrat_16, C_TEXT, 14, 2);
  tstatic(t, "ciclo", &lv_font_montserrat_12, C_FAINT, 430, 6);

  lv_obj_t *c = card(t, 8, GH_CARDY, 464, GH_CARDH);
  lv_obj_set_style_pad_all(c, 10, 0);
  for (int i = 0; i < GH_ROWS; i++)
    build_rank_row(c, 2 + i * GH_ROWH, &g_ui.ghProjLbl[i], &g_ui.ghProjTrk[i],
                   &g_ui.ghProjBar[i], &g_ui.ghProjVal[i]);

  tstatic(t, "MINUTOS POR DIA - 14 DIAS", &lv_font_montserrat_12, C_MUTED, 14, GD_CAPY);
  g_ui.ghDayCap = tlabel(t, &lv_font_montserrat_12, C_FAINT, 330, GD_CAPY);
  lv_obj_set_width(g_ui.ghDayCap, 140);
  lv_obj_set_style_text_align(g_ui.ghDayCap, LV_TEXT_ALIGN_RIGHT, 0);

  for (int d = 0; d < GH_DAYS; d++)
    for (int k = 0; k < GH_PROJ; k++)
      g_ui.ghDaySeg[d][k] = rrect(t, GD_X0 + d * GD_W, GD_BASE, GD_BARW, 2, 0,
                                  proj_color(k));

  // Legenda: sem ela, as cores empilhadas nao dizem nada.
  for (int k = 0; k < GH_PROJ; k++) {
    g_ui.ghLegDot[k] = rrect(t, 0, GD_LEGY + 3, 8, 8, 2, proj_color(k));
    g_ui.ghLegLbl[k] = tlabel(t, &lv_font_montserrat_12, C_FAINT, 0, GD_LEGY);
    lv_obj_set_width(g_ui.ghLegLbl[k], 78);
    lv_label_set_long_mode(g_ui.ghLegLbl[k], LV_LABEL_LONG_DOT);
  }
}

// ----------------------------------------------------------------- JOBS ----

void build_tile_github_jobs(lv_obj_t *t) {
  tstatic(t, "Jobs", &lv_font_montserrat_16, C_TEXT, 14, 2);
  tstatic(t, "ciclo", &lv_font_montserrat_12, C_FAINT, 430, 6);

  lv_obj_t *c = card(t, 8, GH_CARDY, 464, GH_CARDH);
  lv_obj_set_style_pad_all(c, 10, 0);
  for (int i = 0; i < GH_ROWS; i++)
    build_rank_row(c, 2 + i * GH_ROWH, &g_ui.ghJobLbl[i], &g_ui.ghJobTrk[i],
                   &g_ui.ghJobBar[i], &g_ui.ghJobVal[i]);

  // Minuto gasto em run que falhou e minuto pago duas vezes: o push seguinte
  // vai rodar tudo de novo.
  lv_obj_t *f = card(t, 8, 134, 464, 84);
  lv_obj_set_style_pad_all(f, 10, 0);
  tstatic(f, "CI VERMELHO - 7 DIAS", &lv_font_montserrat_12, C_MUTED, 0, 0);
  g_ui.ghCiPct = tlabel(f, &lv_font_montserrat_28, C_TEXT, 0, 18);
  g_ui.ghCiSub = tlabel(f, &lv_font_montserrat_12, C_FAINT, 0, 50);
  lv_obj_set_width(g_ui.ghCiSub, 230);
  lv_label_set_long_mode(g_ui.ghCiSub, LV_LABEL_LONG_DOT);
  g_ui.ghCiPerd = tlabel(f, &lv_font_montserrat_14, C_MUTED, 214, 22);
  lv_obj_set_width(g_ui.ghCiPerd, 230);
  lv_obj_set_style_text_align(g_ui.ghCiPerd, LV_TEXT_ALIGN_RIGHT, 0);
}

// ------------------------------------------------------------- REFRESH -----

void refresh_github(void) {
  const GithubUsage &g = g_github;
  char buf[72];
  auto vis = [](lv_obj_t *o, bool m) { if (!o) return;
    if (m) lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); };

  // ---- AGORA ----
  if (g_ui.ghPct) {
    if (g.ok) snprintf(buf, sizeof(buf), "%u%%", g.pct); else snprintf(buf, sizeof(buf), "--");
    lv_label_set_text(g_ui.ghPct, buf);
    lv_obj_set_style_text_color(g_ui.ghPct, lv_color_hex(g.pagosMin ? C_BAD : C_OK), 0);
  }
  if (g_ui.ghBarFree && g_ui.ghBarPaid) {
    uint32_t tot = g.usadosMin ? g.usadosMin : 1;
    uint32_t livre = g.usadosMin > g.pagosMin ? g.usadosMin - g.pagosMin : 0;
    int wFree = (int)((uint64_t)livre * 204 / tot);
    lv_obj_set_width(g_ui.ghBarFree, g.usadosMin ? (wFree < 2 ? 2 : wFree) : 2);
    lv_obj_set_pos(g_ui.ghBarPaid, wFree, 92);
    lv_obj_set_width(g_ui.ghBarPaid, 204 - wFree < 2 ? 2 : 204 - wFree);
    vis(g_ui.ghBarPaid, g.pagosMin > 0);
  }
  if (g_ui.ghCotaSub) {
    if (!g.ok) snprintf(buf, sizeof(buf), "sem dados");
    else if (g.pagosMin) snprintf(buf, sizeof(buf), "%u MIN - %u GRATIS", g.usadosMin, g.incluidosMin);
    else snprintf(buf, sizeof(buf), "%u / %u MIN", g.usadosMin, g.incluidosMin);
    lv_label_set_text(g_ui.ghCotaSub, buf);
  }
  if (g_ui.ghUsd) {
    // Verde em zero, vermelho a partir do primeiro centavo.
    if (g.ok) snprintf(buf, sizeof(buf), "$%.2f", (double)g.usd); else snprintf(buf, sizeof(buf), "--");
    lv_label_set_text(g_ui.ghUsd, buf);
    lv_obj_set_style_text_color(g_ui.ghUsd, lv_color_hex(g.usd > 0.0f ? C_BAD : C_OK), 0);
  }
  if (g_ui.ghUsdSub) {
    if (!g.ok) snprintf(buf, sizeof(buf), "");
    else if (g.limiteUsd > 0) snprintf(buf, sizeof(buf), "%u%% DO LIMITE - %ud P/ FECHAR",
                                       g.pctLimite, g.diasRestantes);
    else snprintf(buf, sizeof(buf), "%ud PARA FECHAR O CICLO", g.diasRestantes);
    lv_label_set_text(g_ui.ghUsdSub, buf);
  }
  uint32_t maxDia = 1;
  for (uint8_t i = 0; i < g.nDay; i++) if (g.day[i].min > maxDia) maxDia = g.day[i].min;
  for (int i = 0; i < 7; i++) {
    if (!g_ui.ghDia[i]) continue;
    int idx = (int)g.nDay - 7 + i;
    if (idx < 0 || !g.ok) { vis(g_ui.ghDia[i], false); continue; }
    int h = (int)((uint64_t)g.day[idx].min * 46 / maxDia);
    if (h < 2) h = 2;
    lv_obj_set_size(g_ui.ghDia[i], 20, h);
    lv_obj_set_pos(g_ui.ghDia[i], i * 29, 154 - h);
    vis(g_ui.ghDia[i], true);
  }
  int x = 12;
  for (uint8_t i = 0; i < GH_PROJ; i++) {
    if (!g_ui.ghFaixa[i]) continue;
    if (i >= g.nProj || !g.ok || !g.usadosMin) { vis(g_ui.ghFaixa[i], false); continue; }
    int w = (int)((uint64_t)g.proj[i].min * 452 / g.usadosMin);
    if (w < 2) w = 2;
    lv_obj_set_size(g_ui.ghFaixa[i], w, 16);
    lv_obj_set_pos(g_ui.ghFaixa[i], x, 204);
    vis(g_ui.ghFaixa[i], true);
    x += w;
  }
  if (g_ui.ghFxLbl) {
    // "rateio", nao "custo": a cota gratuita e da conta, nao do repo.
    if (!g.ok || !g.nProj) lv_label_set_text(g_ui.ghFxLbl, "");
    else {
      snprintf(buf, sizeof(buf), "%s %u%%%s%s %u%%", g.proj[0].key, g.proj[0].pct,
               g.nProj > 1 ? " - " : "", g.nProj > 1 ? g.proj[1].key : "",
               g.nProj > 1 ? g.proj[1].pct : 0);
      lv_label_set_text(g_ui.ghFxLbl, buf);
    }
  }

  // ---- PROJETOS ----
  for (int i = 0; i < GH_ROWS; i++)
    fill_rank_row(g_ui.ghProjLbl[i], g_ui.ghProjTrk[i], g_ui.ghProjBar[i], g_ui.ghProjVal[i],
                  g.ok && i < g.nProj, g.proj[i].key, g.proj[i].min, g.proj[i].pct,
                  proj_color(i));

  for (int d = 0; d < GH_DAYS; d++) {
    int idx = (int)g.nDay - GH_DAYS + d;
    int yTop = GD_BASE;
    for (int k = 0; k < GH_PROJ; k++) {
      lv_obj_t *s = g_ui.ghDaySeg[d][k];
      if (!s) continue;
      if (idx < 0 || !g.ok || k >= g.nProjOrder || !g.day[idx].v[k]) { vis(s, false); continue; }
      int h = (int)((uint64_t)g.day[idx].v[k] * GD_H / maxDia);
      if (h < 2) h = 2;
      yTop -= h;
      lv_obj_set_size(s, GD_BARW, h);
      lv_obj_set_pos(s, GD_X0 + d * GD_W, yTop);
      vis(s, true);
    }
  }
  if (g_ui.ghDayCap) {
    if (!g.ok || !g.nDay) lv_label_set_text(g_ui.ghDayCap, "coletando...");
    else { snprintf(buf, sizeof(buf), "%s a %s", g.day[0].label, g.day[g.nDay - 1].label);
           lv_label_set_text(g_ui.ghDayCap, buf); }
  }
  // Legenda do grafico: dot + nome, distribuidos na largura util.
  int lx = 14;
  for (uint8_t k = 0; k < GH_PROJ; k++) {
    bool tem = g.ok && k < g.nProjOrder;
    vis(g_ui.ghLegDot[k], tem);
    vis(g_ui.ghLegLbl[k], tem);
    if (!tem) continue;
    lv_obj_set_pos(g_ui.ghLegDot[k], lx, GD_LEGY + 3);
    lv_obj_set_pos(g_ui.ghLegLbl[k], lx + 12, GD_LEGY);
    lv_label_set_text(g_ui.ghLegLbl[k], g.projOrder[k]);
    lx += 96;
  }

  // ---- JOBS ----
  for (int i = 0; i < GH_ROWS; i++)
    fill_rank_row(g_ui.ghJobLbl[i], g_ui.ghJobTrk[i], g_ui.ghJobBar[i], g_ui.ghJobVal[i],
                  g.ok && i < g.nJob, g.job[i].key, g.job[i].min, g.job[i].pct, C_GITHUB);

  if (g_ui.ghCiPct) {
    if (!g.ok || !g.hasCi) {
      lv_label_set_text(g_ui.ghCiPct, "--");
      lv_label_set_text(g_ui.ghCiSub, "sem dados de CI");
      lv_label_set_text(g_ui.ghCiPerd, "");
    } else {
      snprintf(buf, sizeof(buf), "%u%%", g.ciPctFalha);
      lv_label_set_text(g_ui.ghCiPct, buf);
      lv_obj_set_style_text_color(g_ui.ghCiPct,
        lv_color_hex(g.ciPctFalha > 30 ? C_BAD : C_OK), 0);
      snprintf(buf, sizeof(buf), "%s - %u DE %u RUNS", g.ciRepo, g.ciFalhas, g.ciRuns);
      lv_label_set_text(g_ui.ghCiSub, buf);
      snprintf(buf, sizeof(buf), "%u MIN QUEIMADOS\nEM RUN VERMELHO", g.ciMinPerdidos);
      lv_label_set_text(g_ui.ghCiPerd, buf);
    }
  }
}
