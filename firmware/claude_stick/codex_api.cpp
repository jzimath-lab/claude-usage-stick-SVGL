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
