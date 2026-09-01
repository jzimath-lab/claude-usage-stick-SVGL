#pragma once
#include "cotas_parse.h"

// Pull GET /cotas from the LAN station (_http._tcp instance `estacao`).
// Independent of the Claude unified-header poll. Failure here must not
// take Claude down.

bool cotasPoll();                 // resolve + GET; updates g_cotas
bool cotasDue(uint32_t nowMs);    // time for a background poll?
bool cotasIsStale(uint32_t nowMs); // > 2× poll without a fresh snapshot
CotasState& cotasState();
const CotasSource* cotasSource(int idx);  // 0=claude … 4=gemini; may be null/empty
void cotasMarkStationDown();
