#pragma once
#include "api.h"

// Busca as cotas na VPS da estacao. `false` tambem quando o snapshot esta
// velho demais — servir dado envelhecido como fresco e pior que nao servir.
bool pullUsage(UsageData& out);

// Fonte unica de verdade da precedencia: VPS primeiro, Anthropic como reserva.
// Chamada por do_refresh() e por bg_refresh().
bool obterUsage(UsageData& out);
