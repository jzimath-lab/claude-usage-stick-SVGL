#include "codex_api.h"
#include "config.h"
#include "certs.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <string.h>
#include <stdlib.h>

#include "codex_parse.h"

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
