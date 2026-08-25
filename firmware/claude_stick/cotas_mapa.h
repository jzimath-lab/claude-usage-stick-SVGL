#pragma once
// Mapeia o payload de cotas da VPS para o MESMO UsageData que a busca direta
// preenchia. E o que permite a UI nao saber de onde o dado veio: ela continua
// lendo g_usage.h5, g_usage.d7 e os resets.
//
// Funcao pura, sem deps Arduino — testada em firmware/tests/cotas_mapa_test.cpp.
#include <string.h>
#include "cotas_parse.h"
#include "api.h"

// strlcpy nao existe na glibc do runner do CI. Helper proprio mantem host e
// device compilando o MESMO codigo, que e a razao de o parser ser um header.
static void strlcpy_(char* d, const char* s, size_t n) {
    if (!n) return;
    size_t i = 0;
    for (; s[i] && i + 1 < n; i++) d[i] = s[i];
    d[i] = 0;
}

/*
 * `false` quando o provedor claude nao existe ou veio em erro. Nos dois casos
 * `out.error` diz por que.
 *
 * ⚠️ Em erro, h5/d7 ficam ZERADOS e nao com resto do que havia. Percentual
 * residual seria pior que ausencia: a UI mostraria um numero inventado no lugar
 * onde o §7 manda envelhecer o ultimo valor bom.
 */
static bool cotasParaUsage(const CotasVps& c, UsageData& out) {
    memset(&out, 0, sizeof(out));

    const CotaProv* p = cotaAchar(c, "claude");
    if (!p) {
        strlcpy_(out.error, "cotas sem provedor claude", sizeof(out.error));
        out.ok = false;
        return false;
    }
    if (p->temErro) {
        strlcpy_(out.error, p->erro, sizeof(out.error));
        out.ok = false;
        return false;
    }

    for (uint8_t i = 0; i < p->nJanelas; i++) {
        const CotaJanela& j = p->janela[i];
        // Os nomes vem do dashboard-v1 e sao estaveis (kind: session|weekly).
        // Comparacao explicita, e nao por ordem no array: a ordem e do produtor
        // e trocar session por weekly seria invisivel — os dois sao float.
        if (!strcmp(j.tipo, "session")) { out.h5 = j.usadoPct; out.h5ResetEpoch = j.resetaEm; }
        else if (!strcmp(j.tipo, "weekly")) { out.d7 = j.usadoPct; out.d7ResetEpoch = j.resetaEm; }
    }

    out.unifiedResetEpoch = out.d7ResetEpoch ? out.d7ResetEpoch : out.h5ResetEpoch;
    strlcpy_(out.statusOverall, "allowed", sizeof(out.statusOverall));
    out.ok = true;
    return true;
}
