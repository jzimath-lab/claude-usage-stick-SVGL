#include "status.h"
#include "config.h"
#include "tls_client.h"
#include <Arduino.h>
#include <HTTPClient.h>

static bool status_once(ModelStatus& out, bool insecure) {
    Ipv4SecureClient client;
    attach_tls(client, insecure);

    HTTPClient https;
    if (!https.begin(client, STATUS_ENDPOINT)) {
        Serial.println("[STATUS] https_init failed");
        return false;
    }

    https.addHeader("User-Agent", "claude-usage-stick/1.0");
    https.setTimeout(API_TIMEOUT_MS);
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    Serial.printf("[STATUS] GET %s%s\n", STATUS_ENDPOINT, insecure ? " (insecure)" : "");
    int code = https.GET();
    Serial.printf("[STATUS] HTTP %d\n", code);

    if (code != 200) {
        https.end();
        return false;   // mantém o último estado conhecido
    }

    int len = https.getSize();
    if (len > 131072) {
        Serial.printf("[STATUS] body too large (%d)\n", len);
        https.end();
        return false;
    }

    String body = https.getString();
    https.end();

    body.toLowerCase();
    out.haikuUp  = body.indexOf("haiku")  < 0;
    out.sonnetUp = body.indexOf("sonnet") < 0;
    out.opusUp   = body.indexOf("opus")   < 0;
    out.fableUp  = body.indexOf("fable")  < 0;
    out.ok = true;

    Serial.printf("[STATUS] haiku:%d sonnet:%d opus:%d fable:%d\n",
                  out.haikuUp, out.sonnetUp, out.opusUp, out.fableUp);
    return true;
}

bool fetchModelStatus(ModelStatus& out) {
    if (status_once(out, false)) return true;
    Serial.println("[STATUS] retry insecure");
    return status_once(out, true);
}
