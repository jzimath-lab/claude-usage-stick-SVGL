#pragma once
#include "app_state.h"

// Histórico do Codex (2o provider), paralelo ao do Claude (history.*), mas
// baseado na janela SEMANAL (d7) — a de 5h do Codex quase sempre vem null.
// Acumulado no device polando o bridge; enche ao longo dos dias.
#define CX_HIST_MAX 160

struct CxSample { uint32_t t; uint8_t pct; };   // pct = used_percent 7d

void cx_hist_push(float pct7);       // amostra no ring buffer
void cx_save_history();
void cx_load_history();

void cx_trend_redraw();              // tela Janela (7d): histórico + projeção
