#pragma once
// Parser das cotas de IA vindas da VPS da estacao (GET /api/display) — PURO,
// sem deps Arduino, para ser testavel no host. Incluido pelo firmware e pelo
// firmware/tests/cotas_parse_test.cpp, entao o teste cobre o MESMO codigo que
// roda no device.
//
// POR QUE ESTE CAMINHO EXISTE
// A busca propria do Claude (POST api.anthropic.com/v1/messages com token OAuth
// de assinatura) devolve HTTP 401 desde 25/08/2026. Quatro causas foram
// eliminadas na placa — rede, PIN, header beta e versao personificada do
// cliente, esta ultima com o binario conferido. Sobra o token, e as tres
// hipoteses restantes desembocam todas em "gerar credencial nova", que e
// justamente o que o aviso de politica do README desaconselha.
//
// Este parser torna a tela independente daquela decisao: os percentuais passam
// a vir do Mac, via VPS, sem token da Anthropic no aparelho.
//
// ⚠️ REGRA NUMERO x TEXTO (§3 do design do S4). So percentuais e epochs sao
// numeros. `plano` e `erro` sao strings de EXIBICAO — "Plus", "Max 5x",
// "Balance: $8.12" — e vao LITERAIS para a tela. Extrair o 8.12 de
// "Balance: $8.12" acoplaria o firmware a formatacao do CodexBar, que ele e
// livre para mudar sem quebrar contrato.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define COTA_MAX_PROV     8
#define COTA_MAX_JANELA   4
#define COTA_ID_MAX      16
#define COTA_TXT_MAX     40
#define COTA_ERR_MAX     72

struct CotaJanela {
    char     tipo[COTA_ID_MAX];   // session | weekly | ...
    float    usadoPct;
    uint32_t resetaEm;            // epoch unix (0 = ausente)
};

struct CotaProv {
    char       id[COTA_ID_MAX];
    char       nome[COTA_ID_MAX];
    char       plano[COTA_TXT_MAX];   // TEXTO literal
    char       erro[COTA_ERR_MAX];    // TEXTO literal
    bool       temErro;
    uint8_t    nJanelas;
    CotaJanela janela[COTA_MAX_JANELA];
};

struct CotasVps {
    char     geradoEm[24];
    int32_t  idadeS;
    uint8_t  nProv;
    CotaProv prov[COTA_MAX_PROV];
};

// --- utilitarios de varredura, na mesma linha do codex_parse.h --------------

// Acha "key" dentro de [from,to) e devolve o ponteiro logo apos os ':' e espacos.
static const char* cotaValor(const char* from, const char* to, const char* key) {
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* k = strstr(from, pat);
    if (!k || k >= to) return nullptr;
    const char* p = k + strlen(pat);
    while (p < to && (*p == ':' || *p == ' ')) p++;
    return p < to ? p : nullptr;
}

static void cotaStr(const char* from, const char* to, const char* key,
                    char* out, size_t sz, bool* eraNulo = nullptr) {
    out[0] = 0;
    if (eraNulo) *eraNulo = false;
    const char* p = cotaValor(from, to, key);
    if (!p) return;
    if (strncmp(p, "null", 4) == 0) { if (eraNulo) *eraNulo = true; return; }
    if (*p != '"') return;
    p++;
    size_t i = 0;
    while (p < to && *p != '"' && i + 1 < sz) {
        if (*p == '\\' && p + 1 < to) p++;   // \" e \\ viram o proprio char
        out[i++] = *p++;
    }
    out[i] = 0;
}

static bool cotaNum(const char* from, const char* to, const char* key, double& out) {
    const char* p = cotaValor(from, to, key);
    if (!p || strncmp(p, "null", 4) == 0) return false;
    char* fim = nullptr;
    double v = strtod(p, &fim);
    if (fim == p) return false;
    out = v;
    return true;
}

/*
 * ISO-8601 UTC ("2026-08-25T19:12:02Z") -> epoch.
 *
 * ⚠️ NAO usa mktime/timegm de proposito: `mktime` interpreta em fuso LOCAL, e o
 * device roda com TZ do Brasil — daria 3 h de erro num campo que serve para
 * contar quanto falta para o reset. Conta de calendario civil, explicita.
 */
static uint32_t cotaEpoch(const char* iso) {
    int Y, M, D, h, m, s;
    if (!iso || sscanf(iso, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &s) != 6) return 0;
    static const int ACUM[12] = {0,31,59,90,120,151,181,212,243,273,304,334};
    if (M < 1 || M > 12) return 0;
    long dias = (Y - 1970) * 365L + ((Y - 1969) / 4) + ACUM[M - 1] + (D - 1);
    bool bissexto = (Y % 4 == 0 && (Y % 100 != 0 || Y % 400 == 0));
    if (bissexto && M > 2) dias += 1;
    return (uint32_t)(dias * 86400L + h * 3600L + m * 60L + s);
}

// Acha o fim do objeto/array que comeca em `p` (que aponta para '{' ou '[').
static const char* cotaFimBloco(const char* p, const char* limite) {
    if (!p || p >= limite) return limite;
    char ab = *p, fe = (ab == '{') ? '}' : ']';
    int prof = 0;
    bool emStr = false;
    for (const char* q = p; q < limite; q++) {
        if (emStr) {
            if (*q == '\\') { q++; continue; }
            if (*q == '"') emStr = false;
            continue;
        }
        if (*q == '"') { emStr = true; continue; }
        if (*q == ab) prof++;
        else if (*q == fe && --prof == 0) return q + 1;
    }
    return limite;
}

// --- o parser ---------------------------------------------------------------

static bool parseCotas(const char* body, CotasVps& out) {
    memset(&out, 0, sizeof(out));
    if (!body) return false;
    const char* limite = body + strlen(body);

    const char* c = strstr(body, "\"cotas\"");
    if (!c) return false;
    const char* ini = strchr(c, '{');
    if (!ini) return false;
    const char* fim = cotaFimBloco(ini, limite);

    cotaStr(ini, fim, "gerado_em", out.geradoEm, sizeof(out.geradoEm));
    double d;
    out.idadeS = cotaNum(ini, fim, "idade_s", d) ? (int32_t)d : -1;

    const char* pv = strstr(ini, "\"provedores\"");
    if (!pv || pv >= fim) return true;          // sem provedores ainda: valido
    const char* arr = strchr(pv, '[');
    if (!arr) return true;
    const char* arrFim = cotaFimBloco(arr, fim);

    const char* p = arr + 1;
    while (p < arrFim && out.nProv < COTA_MAX_PROV) {
        const char* obj = strchr(p, '{');
        if (!obj || obj >= arrFim) break;
        const char* objFim = cotaFimBloco(obj, arrFim);

        CotaProv& pr = out.prov[out.nProv];
        cotaStr(obj, objFim, "id", pr.id, sizeof(pr.id));
        if (pr.id[0] == 0) { p = objFim; continue; }
        cotaStr(obj, objFim, "nome", pr.nome, sizeof(pr.nome));
        cotaStr(obj, objFim, "plano", pr.plano, sizeof(pr.plano));

        bool erroNulo = false;
        cotaStr(obj, objFim, "erro", pr.erro, sizeof(pr.erro), &erroNulo);
        pr.temErro = !erroNulo && pr.erro[0] != 0;

        const char* jn = strstr(obj, "\"janelas\"");
        if (jn && jn < objFim) {
            const char* ja = strchr(jn, '[');
            const char* jaFim = ja ? cotaFimBloco(ja, objFim) : objFim;
            const char* q = ja ? ja + 1 : objFim;
            while (q < jaFim && pr.nJanelas < COTA_MAX_JANELA) {
                const char* jo = strchr(q, '{');
                if (!jo || jo >= jaFim) break;
                const char* joFim = cotaFimBloco(jo, jaFim);
                CotaJanela& w = pr.janela[pr.nJanelas];
                cotaStr(jo, joFim, "tipo", w.tipo, sizeof(w.tipo));
                w.usadoPct = cotaNum(jo, joFim, "usado_pct", d) ? (float)d : 0.0f;
                char iso[24];
                cotaStr(jo, joFim, "reseta_em", iso, sizeof(iso));
                w.resetaEm = cotaEpoch(iso);
                pr.nJanelas++;
                q = joFim;
            }
        }
        out.nProv++;
        p = objFim;
    }
    return true;
}

static const CotaProv* cotaAchar(const CotasVps& c, const char* id) {
    for (uint8_t i = 0; i < c.nProv; i++)
        if (strcmp(c.prov[i].id, id) == 0) return &c.prov[i];
    return nullptr;
}
