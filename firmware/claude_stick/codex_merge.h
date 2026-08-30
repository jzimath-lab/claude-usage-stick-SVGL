#pragma once
#include <string.h>
#include "codex_api.h"

/*
 * Combina as DUAS fontes do Codex e resolve a frescura de CADA METADE.
 *
 * POR QUE ISTO EXISTE
 * -------------------
 * O payload do Codex na tela vem de dois lugares com ciclos de vida distintos:
 *
 *   janelas (cotas 5h/7d) ... estacao, via /api/display — recusadas se > 900s
 *   analytics (origem,      . bridge codex-usage — unica fonte que as tem
 *   modelo, interacoes)
 *
 * Depois da consolidacao do S4 o codigo fazia `g_codex = bridge` e sobrescrevia
 * so as janelas. A flag `stale` continuava sendo a do bridge: em 30/08 o bridge
 * levou http_401 do backend do ChatGPT e a tela passou a escrever "DADO VELHO"
 * sobre percentuais que estavam FRESCOS, vindos da estacao.
 *
 * Combinar fontes nao e so combinar dados — os METADADOS tambem precisam ser
 * combinados. Uma flag de frescura so por metade do payload.
 *
 *   `stale`   -> as COTAS exibidas estao datadas
 *   `anStale` -> as ANALYTICS exibidas estao datadas
 *
 * Devolve false quando nao ha nada a exibir (as duas fontes fora).
 */
static inline bool codexCombinar(const CodexUsage* bridge, const CodexUsage* estacao,
                                 const CodexUsage& anterior, CodexUsage& out) {
    if (!bridge && !estacao) return false;

    if (bridge) {
        out = *bridge;                 // plano, limites e analytics sao do bridge
        out.anStale = bridge->stale;   // as analytics envelhecem junto com ele

        if (estacao) {
            // Fonte unica das cotas (§7): as janelas sao da estacao, e a
            // frescura das cotas passa a ser A DELA — nao a do bridge.
            out.has5h = estacao->has5h; out.pct5 = estacao->pct5;
            out.reset5Epoch = estacao->reset5Epoch; out.after5 = estacao->after5;
            out.has7d = estacao->has7d; out.pct7 = estacao->pct7;
            out.reset7Epoch = estacao->reset7Epoch; out.after7 = estacao->after7;
            out.stale = estacao->stale;
        }
        return true;
    }

    // Bridge fora do ar: as janelas da estacao seguem valendo, e as analytics do
    // ciclo anterior continuam na tela em vez de zerar. Dado morto na tela e
    // aceitavel; dado morto SEM AVISO nao e — dai o anStale.
    out = *estacao;
    out.stale       = estacao->stale;
    out.hasAn       = anterior.hasAn;
    out.anRangeDays = anterior.anRangeDays;
    out.interactions= anterior.interactions;
    out.anThreads   = anterior.anThreads;
    out.creditsTotal= anterior.creditsTotal;
    out.nSurface    = anterior.nSurface;
    memcpy(out.surface,   anterior.surface,   sizeof(out.surface));
    out.nModel      = anterior.nModel;
    memcpy(out.model,     anterior.model,     sizeof(out.model));
    out.nSurfOrder  = anterior.nSurfOrder;
    memcpy(out.surfOrder, anterior.surfOrder, sizeof(out.surfOrder));
    out.nDay        = anterior.nDay;
    memcpy(out.day,       anterior.day,       sizeof(out.day));
    out.anStale     = true;
    return true;
}
