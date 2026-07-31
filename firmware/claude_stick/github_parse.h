#pragma once
// Parser do JSON de /api/github — PURO (sem deps Arduino), para ser testavel no
// host. Incluido por github_api.cpp (firmware) e por tests/github_parse_test.cpp,
// garantindo que o teste cobre o MESMO codigo que roda no device.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "github_api.h"

// Acha a CHAVE "key" — exigindo os dois-pontos depois dela — e devolve o
// ponteiro para o inicio do VALOR.
//
// Sem a exigencia do ':', strstr casa tambem com um VALOR igual ao nome da
// chave. Medido no payload real: o workflow do helm se chama "ci", entao
// {"nome":"ci"} aparece ANTES da chave {"ci":[...]} e o parser lia o array
// errado — a tela de CI ficava vazia sem erro nenhum. Mesma familia do ']'
// interno: busca ingenua por substring em JSON.
static const char* ghKey(const char* from, const char* to, const char* key) {
    char pat[28];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    size_t n = strlen(pat);
    const char* k = from;
    while ((k = strstr(k, pat)) != nullptr && k < to) {
        const char* p = k + n;
        while (p < to && *p == ' ') p++;
        if (p < to && *p == ':') return p + 1;   // e chave, nao valor
        k += n;                                   // era valor: segue procurando
    }
    return nullptr;
}

// Numero que segue "key": dentro de [from, to).
static bool ghNum(const char* from, const char* to, const char* key, double& out) {
    const char* p = ghKey(from, to, key);
    if (!p) return false;
    while (p < to && *p == ' ') p++;
    if (p >= to) return false;
    if (strncmp(p, "null", 4) == 0) return false;
    char* endp = nullptr;
    double v = strtod(p, &endp);
    if (endp == p) return false;
    out = v;
    return true;
}

// String que segue "key": dentro de [from, to). Copia truncando com seguranca.
static bool ghStr(const char* from, const char* to, const char* key,
                  char* dst, size_t dstSz) {
    const char* p = ghKey(from, to, key);
    if (!p) return false;
    while (p < to && *p == ' ') p++;
    if (p >= to || *p != '"') return false;
    p++;
    const char* e = strchr(p, '"');
    if (!e || e > to) return false;
    size_t n = (size_t)(e - p);
    if (n >= dstSz) n = dstSz - 1;
    memcpy(dst, p, n);
    dst[n] = 0;
    return true;
}

// Fim do array que comeca em `lb` ('['), respeitando UM nivel de aninhamento.
//
// Isto existe por causa de um bug real do parser do Codex: o objeto de cada dia
// contem "v":[...], e procurar o ']' com strchr casava com o ']' INTERNO,
// truncando o array de dias no primeiro. Aqui contamos colchetes.
static const char* ghArrayEnd(const char* lb) {
    int prof = 0;
    for (const char* p = lb; *p; p++) {
        if (*p == '[') prof++;
        else if (*p == ']') { prof--; if (prof == 0) return p; }
    }
    return nullptr;
}

// Itera os objetos de um array e chama fn(inicio, fim) para cada um.
// Para quando o proximo token nao e '{' — ou seja, no ']' do proprio array.
template <typename F>
static uint8_t ghEachObj(const char* body, const char* key, uint8_t max, F fn) {
    const char* k = ghKey(body, body + strlen(body), key);
    if (!k) return 0;
    const char* lb = strchr(k, '[');
    if (!lb) return 0;
    const char* fim = ghArrayEnd(lb);
    if (!fim) return 0;

    uint8_t n = 0;
    const char* p = lb + 1;
    while (n < max && p < fim) {
        while (p < fim && (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r')) p++;
        if (p >= fim || *p != '{') break;
        // O objeto pode conter arrays (v[]), mas nao objetos aninhados.
        const char* oe = strchr(p, '}');
        if (!oe || oe > fim) break;
        fn(p, oe, n);
        n++;
        p = oe + 1;
    }
    return n;
}

// Le "v":[a,b,c] de dentro de um objeto de dia.
static uint8_t ghIntArray(const char* from, const char* to, const char* key,
                          uint16_t* dst, uint8_t max) {
    const char* k = ghKey(from, to, key);
    if (!k) return 0;
    const char* lb = strchr(k, '[');
    if (!lb || lb >= to) return 0;
    uint8_t n = 0;
    const char* p = lb + 1;
    while (n < max && *p && *p != ']') {
        while (*p == ' ' || *p == ',') p++;
        if (*p == ']' || !*p) break;
        char* endp = nullptr;
        double v = strtod(p, &endp);
        if (endp == p) break;
        dst[n++] = (uint16_t)v;
        p = endp;
    }
    return n;
}

// Array de strings: "proj_order":["a","b"]
static uint8_t ghStrArray(const char* body, const char* key,
                          char dst[][20], uint8_t max) {
    const char* k = ghKey(body, body + strlen(body), key);
    if (!k) return 0;
    const char* lb = strchr(k, '[');
    if (!lb) return 0;
    const char* fim = ghArrayEnd(lb);
    if (!fim) return 0;
    uint8_t n = 0;
    const char* p = lb + 1;
    while (n < max && p < fim) {
        const char* q = strchr(p, '"');
        if (!q || q >= fim) break;
        q++;
        const char* e = strchr(q, '"');
        if (!e || e > fim) break;
        size_t len = (size_t)(e - q);
        if (len > 19) len = 19;
        memcpy(dst[n], q, len);
        dst[n][len] = 0;
        n++;
        p = e + 1;
    }
    return n;
}

static bool ghParse(const char* body, GithubUsage& o) {
    memset(&o, 0, sizeof(o));
    o.divergenciaPct = -1.0f;
    if (!body || !*body) { snprintf(o.error, sizeof(o.error), "corpo vazio"); return false; }

    const char* fim = body + strlen(body);
    double v;

    char fonte[16] = {0};
    ghStr(body, fim, "fonte", fonte, sizeof(fonte));
    o.billing = (strcmp(fonte, "billing") == 0);

    // Bloco "cota": delimitar para nao capturar numeros de outros blocos.
    const char* c = strstr(body, "\"cota\"");
    const char* cEnd = c ? strchr(c, '}') : nullptr;
    if (!c || !cEnd) { snprintf(o.error, sizeof(o.error), "sem cota"); return false; }
    if (ghNum(c, cEnd, "usados_min", v))    o.usadosMin = (uint32_t)v;
    if (ghNum(c, cEnd, "incluidos_min", v)) o.incluidosMin = (uint32_t)v;
    if (ghNum(c, cEnd, "pagos_min", v))     o.pagosMin = (uint32_t)v;
    if (ghNum(c, cEnd, "pct", v))           o.pct = (uint16_t)v;

    const char* u = strstr(body, "\"custo\"");
    const char* uEnd = u ? strchr(u, '}') : nullptr;
    if (u && uEnd) {
        if (ghNum(u, uEnd, "usd", v))         o.usd = (float)v;
        if (ghNum(u, uEnd, "limite_usd", v))  o.limiteUsd = (float)v;
        if (ghNum(u, uEnd, "pct_limite", v))  o.pctLimite = (uint8_t)v;
    }

    const char* ci = strstr(body, "\"ciclo\"");
    const char* ciEnd = ci ? strchr(ci, '}') : nullptr;
    if (ci && ciEnd && ghNum(ci, ciEnd, "dias_restantes", v)) o.diasRestantes = (uint8_t)v;

    o.nProj = ghEachObj(body, "projetos", GH_ROWS, [&](const char* a, const char* b, uint8_t i) {
        ghStr(a, b, "repo", o.proj[i].key, sizeof(o.proj[i].key));
        double x;
        if (ghNum(a, b, "min", x)) o.proj[i].min = (uint32_t)x;
        if (ghNum(a, b, "pct", x)) o.proj[i].pct = (uint8_t)x;
        if (ghNum(a, b, "usd", x)) o.proj[i].usd = (float)x;
    });

    o.nJob = ghEachObj(body, "jobs", GH_ROWS, [&](const char* a, const char* b, uint8_t i) {
        ghStr(a, b, "nome", o.job[i].key, sizeof(o.job[i].key));
        double x;
        if (ghNum(a, b, "min", x)) o.job[i].min = (uint32_t)x;
        if (ghNum(a, b, "pct", x)) o.job[i].pct = (uint8_t)x;
    });

    o.nProjOrder = ghStrArray(body, "proj_order", o.projOrder, GH_PROJ);

    o.nDay = ghEachObj(body, "diario", GH_DAYS, [&](const char* a, const char* b, uint8_t i) {
        ghStr(a, b, "d", o.day[i].label, sizeof(o.day[i].label));
        double x;
        if (ghNum(a, b, "min", x)) o.day[i].min = (uint32_t)x;
        ghIntArray(a, b, "v", o.day[i].v, GH_PROJ);
    });

    // CI: fica o repo com mais minutos perdidos — e o que cabe no rodape e o
    // que responde "onde o desperdicio esta".
    uint32_t pior = 0;
    ghEachObj(body, "ci", GH_ROWS, [&](const char* a, const char* b, uint8_t) {
        double x;
        uint32_t perd = ghNum(a, b, "min_desperdicados", x) ? (uint32_t)x : 0;
        if (!o.hasCi || perd > pior) {
            pior = perd;
            o.hasCi = true;
            o.ciMinPerdidos = perd;
            ghStr(a, b, "repo", o.ciRepo, sizeof(o.ciRepo));
            if (ghNum(a, b, "runs", x))      o.ciRuns = (uint16_t)x;
            if (ghNum(a, b, "falhas", x))    o.ciFalhas = (uint16_t)x;
            if (ghNum(a, b, "pct_falha", x)) o.ciPctFalha = (uint8_t)x;
        }
    });

    if (ghNum(body, fim, "divergencia_pct", v)) o.divergenciaPct = (float)v;
    if (ghNum(body, fim, "ts", v)) o.ts = (uint32_t)v;

    o.ok = true;
    return true;
}
