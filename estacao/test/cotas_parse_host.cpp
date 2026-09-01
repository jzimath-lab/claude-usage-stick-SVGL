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

  // Live Codex tile: percent + reset. Missing usedPct on 7d stays no_source.
  const char *codexLive =
    "{\"sources\":[{\"source\":\"codex\",\"windows\":["
    "{\"name\":\"5h\",\"usedPct\":28,\"resetAt\":\"2026-08-31T19:15:00.000Z\",\"status\":\"ok\"},"
    "{\"name\":\"7d\",\"status\":\"no_source\"}]}]}";
  CotasState st3;
  memset(&st3, 0, sizeof(st3));
  assert(cotasParse(codexLive, st3));
  assert(strcmp(st3.src[1].id, "codex") == 0);
  assert(st3.src[1].win[0].hasPct);
  assert(st3.src[1].win[0].usedPct == 28);
  assert(st3.src[1].win[0].status == COTAS_OK);
  assert(st3.src[1].win[0].resetEpoch == 1788203700); // 2026-08-31T19:15:00Z
  assert(!st3.src[1].win[1].hasPct);
  assert(st3.src[1].win[1].status == COTAS_NOSRC);

  // Live Cursor: included vs on-demand. Grok only when usagePercent is present.
  const char *cursorLive =
    "{\"sources\":[{\"source\":\"cursor\",\"windows\":["
    "{\"name\":\"incluido\",\"usedPct\":41,\"resetAt\":\"2026-09-15T00:00:00.000Z\",\"status\":\"ok\"},"
    "{\"name\":\"on_demand\",\"usedPct\":21,\"usedAbsolute\":4.2,\"unit\":\"usd\",\"status\":\"ok\"},"
    "{\"name\":\"grok_bot\",\"usedPct\":12,\"resetAt\":\"2026-09-07T00:00:00.000Z\",\"status\":\"ok\"}]}]}";
  CotasState st4;
  memset(&st4, 0, sizeof(st4));
  assert(cotasParse(cursorLive, st4));
  assert(strcmp(st4.src[2].id, "cursor") == 0);
  assert(st4.src[2].nWin == 3);
  assert(st4.src[2].win[0].hasPct);
  assert(st4.src[2].win[0].usedPct == 41);
  assert(st4.src[2].win[1].hasPct);
  assert(st4.src[2].win[1].usedPct == 21);
  assert(st4.src[2].win[2].hasPct);
  assert(st4.src[2].win[2].usedPct == 12);

  // Grok omitted ≠ 0%. Parser must not invent a third window percent.
  const char *cursorNoGrok =
    "{\"sources\":[{\"source\":\"cursor\",\"windows\":["
    "{\"name\":\"incluido\",\"usedPct\":5,\"status\":\"ok\"},"
    "{\"name\":\"on_demand\",\"status\":\"no_source\"}]}]}";
  CotasState st5;
  memset(&st5, 0, sizeof(st5));
  assert(cotasParse(cursorNoGrok, st5));
  assert(st5.src[2].nWin == 2);
  assert(st5.src[2].win[0].usedPct == 5);
  assert(!st5.src[2].win[2].hasPct);

  // Live Gemini: hoje % after probe. Missing ciclo stays no_source (never 0%).
  const char *geminiLive =
    "{\"sources\":[{\"source\":\"gemini\",\"windows\":["
    "{\"name\":\"hoje\",\"usedPct\":42,\"resetAt\":\"2026-09-01T07:00:00.000Z\",\"status\":\"ok\"},"
    "{\"name\":\"ciclo\",\"status\":\"no_source\"}]}]}";
  CotasState st6;
  memset(&st6, 0, sizeof(st6));
  assert(cotasParse(geminiLive, st6));
  assert(strcmp(st6.src[4].id, "gemini") == 0);
  assert(st6.src[4].win[0].hasPct);
  assert(st6.src[4].win[0].usedPct == 42);
  assert(st6.src[4].win[0].status == COTAS_OK);
  assert(st6.src[4].win[0].resetEpoch == 1788246000); // 2026-09-01T07:00:00Z
  assert(!st6.src[4].win[1].hasPct);
  assert(st6.src[4].win[1].status == COTAS_NOSRC);

  // ZYN-573: mDNS matches the service INSTANCE, not the machine hostname.
  assert(cotasInstanceIsEstacao("estacao", "estacao"));
  assert(cotasInstanceIsEstacao("Estacao", "estacao"));
  assert(cotasInstanceIsEstacao("estacao.local", "estacao"));
  assert(!cotasInstanceIsEstacao("my-laptop", "estacao"));
  assert(!cotasInstanceIsEstacao("estacao-pi", "estacao")); // hostname-style, not instance
  assert(!cotasInstanceIsEstacao("", "estacao"));
  // TXT path=/cotas is optional metadata, not an alternative identity.
  assert(cotasSelectEstacaoService("estacao", "estacao", "/cotas"));
  assert(cotasSelectEstacaoService("estacao", "estacao", nullptr));
  assert(cotasSelectEstacaoService("Estacao", "estacao", "/other"));
  assert(!cotasSelectEstacaoService("other-box", "estacao", "/cotas"));
  assert(!cotasSelectEstacaoService("claude-stick", "estacao", "/cotas"));
  assert(!cotasSelectEstacaoService("my-laptop", "estacao", "/cotas"));
  assert(!cotasSelectEstacaoService("", "estacao", "/cotas"));

  // ZYN-573: MINUTOS big number is usedAbsolute; omitted % stays omitted.
  char big[32];
  assert(cotasFormatBig(st.src[3].win[0], big, sizeof(big)));
  assert(strcmp(big, "731") == 0);          // not "37%"
  assert(cotasFormatBig(st.src[3].win[1], big, sizeof(big)));
  assert(strcmp(big, "$0.00") == 0);        // measured 0 USD
  // Cursor on_demand with both fields keeps % (not $4.20).
  assert(cotasFormatBig(st4.src[2].win[1], big, sizeof(big)));
  assert(strcmp(big, "21%") == 0);
  CotasWindow usdOnly;
  memset(&usdOnly, 0, sizeof(usdOnly));
  strcpy(usdOnly.name, "on_demand");
  usdOnly.hasAbs = true;
  usdOnly.usedAbs = 4.2f;
  strcpy(usdOnly.unit, "usd");
  usdOnly.status = COTAS_OK;
  assert(cotasFormatBig(usdOnly, big, sizeof(big)));
  assert(strcmp(big, "$4.20") == 0);        // USD-only Cursor still shows dollars
  CotasWindow pctOnly;
  memset(&pctOnly, 0, sizeof(pctOnly));
  pctOnly.hasPct = true;
  pctOnly.usedPct = 28;
  pctOnly.status = COTAS_OK;
  assert(cotasFormatBig(pctOnly, big, sizeof(big)));
  assert(strcmp(big, "28%") == 0);
  assert(cotasFormatBig(st6.src[4].win[0], big, sizeof(big)));
  assert(strcmp(big, "42%") == 0);
  assert(!cotasFormatBig(st6.src[4].win[1], big, sizeof(big)));
  CotasWindow omitted;
  memset(&omitted, 0, sizeof(omitted));
  omitted.status = COTAS_NOSRC;
  assert(!cotasFormatBig(omitted, big, sizeof(big)));

  puts("cotas_parse_host: ok");
  return 0;
}
