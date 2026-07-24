#include "codex_api.h"
#include "config.h"
#include "certs.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <string.h>
#include <stdlib.h>

// ---- Parser mínimo para o JSON do bridge ----
// O corpo é pequeno e de forma fixa; não vale trazer uma lib de JSON só p/ isto.
// Estratégia: achar a chave, pular ':' e espaços, ler o que vier.

// Lê o número que segue "key": dentro da faixa [from, to). Retorna false se não achar.
static bool numAfter(const char* from, const char* to, const char* key, double& out) {
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* k = strstr(from, pat);
    if (!k || k >= to) return false;
    const char* p = k + strlen(pat);
    while (p < to && (*p == ':' || *p == ' ')) p++;
    if (p >= to) return false;
    char* endp = nullptr;
    double v = strtod(p, &endp);
    if (endp == p) return false;
    out = v;
    return true;
}

// Extrai a janela "h5"/"d7". Se vier null → present=false (sem erro).
static void parseWindow(const char* body, const char* key,
                        bool& present, float& pct, uint32_t& after, uint32_t& resetEpoch) {
    present = false; pct = 0; after = 0; resetEpoch = 0;
    char pat[16];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* k = strstr(body, pat);
    if (!k) return;
    const char* p = k + strlen(pat);
    while (*p == ':' || *p == ' ') p++;
    if (strncmp(p, "null", 4) == 0) return;   // janela ausente (sem uso recente)
    if (*p != '{') return;
    const char* end = strchr(p, '}');
    if (!end) return;

    double v;
    if (numAfter(p, end, "used_percent", v)) pct = (float)v;
    if (numAfter(p, end, "reset_after", v)) after = (uint32_t)v;
    if (numAfter(p, end, "reset_at", v))    resetEpoch = (uint32_t)v;
    present = true;
}

static bool boolField(const char* body, const char* key, bool dflt) {
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* k = strstr(body, pat);
    if (!k) return dflt;
    const char* p = k + strlen(pat);
    while (*p == ':' || *p == ' ') p++;
    return strncmp(p, "true", 4) == 0;
}

static void strField(const char* body, const char* key, char* out, size_t sz) {
    out[0] = 0;
    char pat[24];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* k = strstr(body, pat);
    if (!k) return;
    const char* p = k + strlen(pat);
    while (*p == ':' || *p == ' ') p++;
    if (*p != '"') return;
    p++;
    const char* e = strchr(p, '"');
    if (!e) return;
    size_t n = (size_t)(e - p);
    if (n >= sz) n = sz - 1;
    memcpy(out, p, n);
    out[n] = 0;
}

// Lê uma string "key":"valor" dentro da faixa [from, to). out[0]=0 se não achar.
static void strAfter(const char* from, const char* to, const char* key, char* out, size_t sz) {
    out[0] = 0;
    char pat[16];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* k = strstr(from, pat);
    if (!k || k >= to) return;
    const char* p = k + strlen(pat);
    while (p < to && (*p == ':' || *p == ' ')) p++;
    if (p >= to || *p != '"') return;
    p++;
    const char* e = strchr(p, '"');
    if (!e || e > to) return;
    size_t n = (size_t)(e - p);
    if (n >= sz) n = sz - 1;
    memcpy(out, p, n);
    out[n] = 0;
}

// Itera os objetos {..} de um array [arr,end) preenchendo CxAnItem (origem/modelo).
static uint8_t parseItems(const char* arr, const char* end, CxAnItem* out,
                          const char* keyName, const char* valName) {
    uint8_t n = 0;
    const char* p = arr;
    while (n < CXAN_ROWS) {
        const char* ob = strchr(p, '{');
        if (!ob || ob >= end) break;
        const char* oe = strchr(ob, '}');
        if (!oe || oe > end) break;
        double v;
        strAfter(ob, oe, keyName, out[n].key, sizeof(out[n].key));
        out[n].val = numAfter(ob, oe, valName, v) ? (float)v : 0.0f;
        out[n].pct = numAfter(ob, oe, "pct", v) ? (uint8_t)(v + 0.5) : 0;
        n++;
        p = oe + 1;
    }
    return n;
}

// Lê um array de inteiros "key":[a,b,c] dentro de [from,to). Retorna quantos leu.
static uint8_t parseIntArray(const char* from, const char* to, const char* key,
                             uint16_t* out, uint8_t maxN) {
    char pat[16]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* k = strstr(from, pat);
    if (!k || k >= to) return 0;
    const char* lb = strchr(k, '['); if (!lb || lb >= to) return 0;
    const char* rb = strchr(lb, ']'); if (!rb || rb > to) return 0;
    uint8_t n = 0; const char* p = lb + 1;
    while (n < maxN && p < rb) {
        while (p < rb && (*p == ' ' || *p == ',')) p++;
        if (p >= rb) break;
        char* endp = nullptr; double val = strtod(p, &endp);
        if (endp == p) break;
        out[n++] = (uint16_t)(val + 0.5); p = endp;
    }
    return n;
}

// Lê um array de strings "key":["a","b"] a partir de `from`. Retorna quantos leu.
static uint8_t parseStrArray(const char* from, const char* key,
                             char out[][14], uint8_t maxN) {
    char pat[16]; snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char* k = strstr(from, pat); if (!k) return 0;
    const char* lb = strchr(k, '['); const char* rb = lb ? strchr(lb, ']') : nullptr;
    if (!lb || !rb) return 0;
    uint8_t n = 0; const char* p = lb;
    while (n < maxN) {
        const char* q = strchr(p, '"'); if (!q || q > rb) break;
        const char* e = strchr(q + 1, '"'); if (!e || e > rb) break;
        size_t len = (size_t)(e - q - 1); if (len >= 14) len = 13;
        memcpy(out[n], q + 1, len); out[n][len] = 0; n++;
        p = e + 1;
    }
    return n;
}

// Extrai a seção "an" (analytics do Codex Cloud). Ausente em bridges antigos → hasAn=false.
static void parseAnalytics(const char* body, CodexUsage& out) {
    const char* an = strstr(body, "\"an\"");
    if (!an) return;
    const char* end = body + strlen(body);
    double v;
    if (numAfter(an, end, "range_days", v))    out.anRangeDays  = (uint16_t)v;
    if (numAfter(an, end, "interactions", v))  out.interactions = (uint32_t)v;
    if (numAfter(an, end, "threads", v))       out.anThreads    = (uint32_t)v;
    if (numAfter(an, end, "credits_total", v)) out.creditsTotal = (float)v;

    const char* s = strstr(an, "\"by_surface\"");
    if (s) {
        const char* lb = strchr(s, '['); const char* rb = lb ? strchr(lb, ']') : nullptr;
        if (lb && rb) out.nSurface = parseItems(lb, rb, out.surface, "src", "credits");
    }
    const char* m = strstr(an, "\"by_model\"");
    if (m) {
        const char* lb = strchr(m, '['); const char* rb = lb ? strchr(lb, ']') : nullptr;
        if (lb && rb) out.nModel = parseItems(lb, rb, out.model, "model", "turns");
    }
    out.nSurfOrder = parseStrArray(an, "surf_order", out.surfOrder, CXAN_SURF);
    // daily: NÃO usar strchr(lb,']') p/ o fim do array — cada dia tem um "v":[...]
    // cujo ']' interno enganaria o parser. Itera objetos e para quando o próximo
    // token não é '{' (ou seja, chegou no ']' do array ou no fim da string).
    const char* d = strstr(an, "\"daily\"");
    if (d) {
        const char* lb = strchr(d, '[');
        if (lb) {
            uint8_t n = 0; const char* p = lb + 1;
            while (n < CXAN_DAYS) {
                while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r') p++;
                if (*p != '{') break;                 // ']' do array daily ou fim
                const char* ob = p;
                const char* oe = strchr(ob, '}');     // objeto do dia não tem chave aninhada
                if (!oe) break;
                strAfter(ob, oe, "d", out.day[n].label, sizeof(out.day[n].label));
                out.day[n].credits = numAfter(ob, oe, "credits", v) ? (uint16_t)v : 0;
                out.day[n].turns   = numAfter(ob, oe, "turns", v)   ? (uint16_t)v : 0;
                parseIntArray(ob, oe, "v", out.day[n].v, CXAN_SURF);   // créditos por origem
                n++; p = oe + 1;
            }
            out.nDay = n;
        }
    }
    out.hasAn = (out.nSurface > 0 || out.nModel > 0 || out.nDay > 0 || out.interactions > 0);
}

bool fetchCodexUsage(const char* url, const char* basicAuthB64,
                     const char* bridgeToken, CodexUsage& out) {
    memset(&out, 0, sizeof(out));

    WiFiClientSecure client;
    client.setCACert(CA_BUNDLE);   // VPS usa Let's Encrypt (raiz no bundle padrão)

    HTTPClient https;
    if (!https.begin(client, url)) {
        strlcpy(out.error, "https_init", sizeof(out.error));
        return false;
    }
    if (basicAuthB64 && basicAuthB64[0])
        https.addHeader("Authorization", String("Basic ") + basicAuthB64);
    if (bridgeToken && bridgeToken[0])
        https.addHeader("X-Bridge-Token", bridgeToken);
    https.setTimeout(API_TIMEOUT_MS);

    int code = https.GET();
    if (code != 200) {
        snprintf(out.error, sizeof(out.error), "http_%d", code);
        https.end();
        return false;
    }
    String body = https.getString();
    https.end();
    const char* b = body.c_str();

    parseWindow(b, "h5", out.has5h, out.pct5, out.after5, out.reset5Epoch);
    parseWindow(b, "d7", out.has7d, out.pct7, out.after7, out.reset7Epoch);
    parseAnalytics(b, out);
    out.allowed      = boolField(b, "allowed", true);
    out.limitReached = boolField(b, "limit_reached", false);
    strField(b, "plan", out.plan, sizeof(out.plan));

    // "ok":false no bridge = dado stale (último-bom); ainda exibível, mas datado.
    out.stale = !boolField(b, "ok", true);
    if (out.stale) strField(b, "error", out.error, sizeof(out.error));

    // Válido se ao menos uma janela veio. Nenhuma + não-stale = resposta estranha.
    out.ok = out.has5h || out.has7d || out.stale;
    if (!out.ok) strlcpy(out.error, "no_windows", sizeof(out.error));
    return out.ok;
}
