// Host test for firmware/claude_stick/cotas_parse.h — same code the ESP32 runs.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../firmware/claude_stick/cotas_parse.h"

static const char *kBody =
  "{\"asOf\":\"2026-08-31T12:00:00.000Z\",\"sources\":["
  "{\"source\":\"claude\",\"label\":\"Claude\",\"windows\":["
    "{\"name\":\"5h\",\"status\":\"no_source\"},{\"name\":\"7d\",\"status\":\"no_source\"}],"
    "\"asOf\":\"2026-08-31T12:00:00.000Z\"},"
  "{\"source\":\"codex\",\"label\":\"Codex\",\"windows\":["
    "{\"name\":\"5h\",\"status\":\"no_source\"}],\"asOf\":\"2026-08-31T12:00:00.000Z\"},"
  "{\"source\":\"cursor\",\"label\":\"Cursor\",\"windows\":["
    "{\"name\":\"incluido\",\"status\":\"no_source\"}],\"asOf\":\"2026-08-31T12:00:00.000Z\"},"
  "{\"source\":\"actions\",\"label\":\"GitHub Actions\",\"windows\":["
    "{\"name\":\"minutos\",\"usedPct\":37,\"usedAbsolute\":731,\"unit\":\"min\","
      "\"resetAt\":\"2026-09-01T00:00:00.000Z\",\"status\":\"ok\"},"
    "{\"name\":\"a_pagar\",\"usedAbsolute\":0,\"unit\":\"usd\",\"status\":\"ok\"}],"
    "\"asOf\":\"2026-08-31T12:00:00.000Z\"},"
  "{\"source\":\"gemini\",\"label\":\"Gemini\",\"windows\":["
    "{\"name\":\"hoje\",\"status\":\"no_source\"}],\"asOf\":\"2026-08-31T12:00:00.000Z\"}"
  "]}";

int main() {
  CotasState st;
  memset(&st, 0, sizeof(st));
  assert(cotasParse(kBody, st));

  assert(st.src[0].have);
  assert(st.src[0].win[0].status == COTAS_NOSRC);
  assert(!st.src[0].win[0].hasPct);

  assert(st.src[3].have);
  assert(strcmp(st.src[3].id, "actions") == 0);
  assert(st.src[3].win[0].hasPct);
  assert(st.src[3].win[0].usedPct == 37);
  assert(st.src[3].win[0].hasAbs);
  assert(st.src[3].win[0].usedAbs == 731);
  assert(st.src[3].win[0].status == COTAS_OK);
  assert(st.src[3].win[0].resetEpoch == 1788220800); // 2026-09-01T00:00:00Z
  assert(st.src[3].win[1].hasAbs);
  assert(st.src[3].win[1].usedAbs == 0); // measured 0 USD is real
  assert(!st.src[3].win[1].hasPct);

  // Missing usedPct must not become 0
  const char *missing =
    "{\"sources\":[{\"source\":\"actions\",\"windows\":["
    "{\"name\":\"minutos\",\"usedAbsolute\":300,\"unit\":\"min\",\"status\":\"ok\"}]}]}";
  CotasState st2;
  memset(&st2, 0, sizeof(st2));
  assert(cotasParse(missing, st2));
  assert(st2.src[3].win[0].hasAbs);
  assert(!st2.src[3].win[0].hasPct);

  puts("cotas_parse_host: ok");
  return 0;
}
