#pragma once
#include "api.h"
#include "codex_api.h"
#include "github_api.h"

// Busca as cotas na VPS da estacao. `false` tambem quando o snapshot esta
// velho demais — servir dado envelhecido como fresco e pior que nao servir.
bool pullUsage(UsageData& out);

// Fonte unica de verdade da precedencia: VPS primeiro, Anthropic como reserva.
// Chamada por do_refresh() e por bg_refresh().
bool obterUsage(UsageData& out);

// Extraem do MESMO corpo que pullUsage() ja baixou — sem nova requisicao.
// `false` quando o pull nao rodou ou o bloco nao veio; o chamador entao usa a
// busca propria como reserva.
bool pullGithub(GithubUsage& out);
bool pullCodex(CodexUsage& out);
