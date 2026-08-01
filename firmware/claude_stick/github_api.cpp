#include "github_api.h"
#include "config.h"
#include "certs.h"
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <string.h>

#include "github_parse.h"

bool fetchGithubUsage(const char* url, const char* basicAuthB64,
                      const char* deviceToken, GithubUsage& out) {
    memset(&out, 0, sizeof(out));
    out.divergenciaPct = -1.0f;

    WiFiClientSecure client;
    client.setCACert(CA_BUNDLE);   // VPS usa Let's Encrypt (raiz no bundle padrao)

    HTTPClient https;
    if (!https.begin(client, url)) {
        strlcpy(out.error, "https_init", sizeof(out.error));
        return false;
    }
    if (basicAuthB64 && basicAuthB64[0])
        https.addHeader("Authorization", String("Basic ") + basicAuthB64);
    // Header PROPRIO: o basic auth da borda ja ocupou o Authorization, entao o
    // token do device nao teria por onde passar como Bearer.
    if (deviceToken && deviceToken[0])
        https.addHeader("X-Device-Token", deviceToken);
    https.setTimeout(API_TIMEOUT_MS);

    int code = https.GET();
    if (code != 200) {
        snprintf(out.error, sizeof(out.error), "http_%d", code);
        https.end();
        return false;
    }
    String body = https.getString();
    https.end();

    return ghParse(body.c_str(), out);
}
