#include "api.h"
#include "config.h"
#include "certs.h"
#include "tls_client.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <esp_heap_caps.h>

#define H5U "anthropic-ratelimit-unified-5h-utilization"
#define H5R "anthropic-ratelimit-unified-5h-reset"
#define H5S "anthropic-ratelimit-unified-5h-status"
#define D7U "anthropic-ratelimit-unified-7d-utilization"
#define D7R "anthropic-ratelimit-unified-7d-reset"
#define D7S "anthropic-ratelimit-unified-7d-status"
#define UST "anthropic-ratelimit-unified-status"
#define URS "anthropic-ratelimit-unified-reset"
#define URC "anthropic-ratelimit-unified-representative-claim"
#define UFB "anthropic-ratelimit-unified-fallback-percentage"
#define UOS "anthropic-ratelimit-unified-overage-status"
#define UOR "anthropic-ratelimit-unified-overage-disabled-reason"

static const char* RL_HEADERS[] = {
    H5U, H5R, H5S, D7U, D7R, D7S, UST, URS, URC, UFB, UOS, UOR
};
static const int RL_HEADER_COUNT = 12;

static void fill_http_error(UsageData& out, WiFiClientSecure& client, int code) {
    char sslerr[96] = {0};
    int tlsCode = client.lastError(sslerr, sizeof(sslerr));
    String why = HTTPClient::errorToString(code);
    Serial.printf("[API] fail code=%d http='%s' tls=%d '%s' internal=%u largest=%u\n",
                  code, why.c_str(), tlsCode, sslerr,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (tlsCode != 0 && sslerr[0])
        snprintf(out.error, sizeof(out.error), "TLS %s", sslerr);
    else if (why.length())
        snprintf(out.error, sizeof(out.error), "%s", why.c_str());
    else
        snprintf(out.error, sizeof(out.error), "http_%d", code);
}

// Uma tentativa por chamada; libera os recursos antes da próxima atualização.
static bool fetch_once(const char* token, UsageData& out) {
    Ipv4SecureClient client;
    HTTPClient https;
    attach_tls(client, CA_API);

    bool ok = false;
    do {
        if (!https.begin(client, MESSAGES_ENDPOINT)) {
            strlcpy(out.error, "https_init", sizeof(out.error));
            break;
        }

        https.addHeader("Authorization", String("Bearer ") + token);
        https.addHeader("anthropic-version", ANTHROPIC_VERSION);
        https.addHeader("anthropic-beta", "oauth-2025-04-20");
        https.addHeader("content-type", "application/json");
        https.addHeader("User-Agent", "claude-code/2.1.5");
        https.setTimeout(API_TIMEOUT_MS);
        https.collectHeaders(RL_HEADERS, RL_HEADER_COUNT);

        String body = "{\"model\":\"" PROBE_MODEL "\","
                      "\"max_tokens\":1,"
                      "\"messages\":[{\"role\":\"user\",\"content\":\".\"}]}";

        Serial.printf("[API] POST %s heap=%u\n",
                      MESSAGES_ENDPOINT,
                      (unsigned)ESP.getFreeHeap());
        int code = https.POST(body);
        Serial.printf("[API] HTTP %d\n", code);

        if (code <= 0) {
            fill_http_error(out, client, code);
            break;
        }

        // Authentication failures must never be hidden by a TLS retry.
        if (code == 401 || code == 403) {
            strlcpy(out.error, code == 401 ? "auth_failed" : "auth_forbidden", sizeof(out.error));
            break;
        }
        String h5u = https.header(H5U);
        String d7u = https.header(D7U);

        if (h5u.length() == 0 && d7u.length() == 0) {
            snprintf(out.error, sizeof(out.error), "no_usage_h_%d", code);
            break;
        }

        out.h5 = h5u.toFloat() * 100.0f;
        out.d7 = d7u.toFloat() * 100.0f;
        out.h5ResetEpoch = (uint32_t)https.header(H5R).toInt();
        out.d7ResetEpoch = (uint32_t)https.header(D7R).toInt();
        out.unifiedResetEpoch = (uint32_t)https.header(URS).toInt();
        out.fallbackPct = https.header(UFB).toFloat() * 100.0f;

        strlcpy(out.statusOverall, https.header(UST).c_str(), sizeof(out.statusOverall));
        strlcpy(out.status5h,      https.header(H5S).c_str(), sizeof(out.status5h));
        strlcpy(out.status7d,      https.header(D7S).c_str(), sizeof(out.status7d));
        strlcpy(out.repClaim,      https.header(URC).c_str(), sizeof(out.repClaim));
        strlcpy(out.overageStatus, https.header(UOS).c_str(), sizeof(out.overageStatus));
        strlcpy(out.overageReason, https.header(UOR).c_str(), sizeof(out.overageReason));

        Serial.printf("[API] 5h:%.0f%% (%s)  7d:%.0f%% (%s)  claim:%s  overall:%s\n",
                      out.h5, out.status5h, out.d7, out.status7d, out.repClaim, out.statusOverall);
        ok = true;
    } while (0);

    https.end();
    client.stop();
    out.ok = ok;
    return ok;
}

bool fetchUsage(const char* token, UsageData& out) {
    out = {};
    return fetch_once(token, out);
}

bool probeModel(const char* token, const char* modelId, ProbeResult& out) {
    Ipv4SecureClient client;
    HTTPClient https;
    attach_tls(client, CA_API);

    if (!https.begin(client, MESSAGES_ENDPOINT)) {
        out.code = -1;
        out.ms = 0;
        https.end();
        client.stop();
        return false;
    }

    https.addHeader("Authorization", String("Bearer ") + token);
    https.addHeader("anthropic-version", ANTHROPIC_VERSION);
    https.addHeader("anthropic-beta", "oauth-2025-04-20");
    https.addHeader("content-type", "application/json");
    https.addHeader("User-Agent", "claude-code/2.1.5");
    https.setTimeout(API_TIMEOUT_MS);

    String body = String("{\"model\":\"") + modelId + "\","
                  "\"max_tokens\":1,"
                  "\"messages\":[{\"role\":\"user\",\"content\":\".\"}]}";

    uint32_t t0 = millis();
    int code = https.POST(body);
    uint32_t dt = millis() - t0;
    https.end();
    client.stop();

    out.code = code;
    out.ms = (dt > 65000) ? 65000 : (uint16_t)dt;
    Serial.printf("[PROBE] %s -> HTTP %d (%ums)\n", modelId, code, (unsigned)dt);
    return code == 200;
}
