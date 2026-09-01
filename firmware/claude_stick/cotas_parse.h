#pragma once
// Parser of GET /cotas (QuotaSnapshot[]). PURO — no Arduino, no cookies,
// no vscdb / auth.json / JSONL. The stick only paints this payload.
//
// usedPct is optional. Missing key → hasPct=false (SEM FONTE), NEVER invent 0.

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define COTAS_NSRC 5
#define COTAS_NW   3

enum CotasStatus : uint8_t {
  COTAS_OK = 0,
  COTAS_WARN,
  COTAS_BLOCKED,
  COTAS_STALE,
  COTAS_NOSRC
};

struct CotasWindow {
  char     name[16];
  bool     hasPct;
  bool     hasAbs;
  float    usedPct;
  float    usedAbs;
  char     unit[8];
  uint32_t resetEpoch;
  CotasStatus status;
};

struct CotasSource {
  char        id[12];
  char        label[28];
  CotasWindow win[COTAS_NW];
  uint8_t     nWin;
  bool        have;
  char        error[48];
};

struct CotasState {
  CotasSource src[COTAS_NSRC];
  bool        stationUp;
  uint32_t    atMs;
};

static const char *const kCotasId[COTAS_NSRC] = {
  "claude", "codex", "cursor", "actions", "gemini"
};

static CotasStatus cotasStatusOf(const char *s) {
  if (!s || !s[0]) return COTAS_NOSRC;
  if (!strcmp(s, "ok")) return COTAS_OK;
  if (!strcmp(s, "warning")) return COTAS_WARN;
  if (!strcmp(s, "blocked")) return COTAS_BLOCKED;
  if (!strcmp(s, "stale")) return COTAS_STALE;
  return COTAS_NOSRC;
}

static int cotasSrcIndex(const char *id) {
  if (!id) return -1;
  for (int i = 0; i < COTAS_NSRC; i++)
    if (!strcmp(id, kCotasId[i])) return i;
  return -1;
}

// Key "foo": — colon required so a VALUE equal to the key is not a match
// (same trap as G1 github_parse: workflow named "ci").
static const char* cqKey(const char* from, const char* to, const char* key) {
  char pat[28];
  snprintf(pat, sizeof(pat), "\"%s\"", key);
  size_t n = strlen(pat);
  const char* k = from;
  while ((k = strstr(k, pat)) != nullptr && k < to) {
    const char* p = k + n;
    while (p < to && (*p == ' ' || *p == '\n')) p++;
    if (p < to && *p == ':') {
      p++;
      while (p < to && (*p == ' ' || *p == '\n')) p++;
      return p;
    }
    k += n;
  }
  return nullptr;
}

static const char* cqMatchBrace(const char* open, const char* lim) {
  if (!open || *open != '{') return nullptr;
  int depth = 0;
  for (const char* p = open; p < lim && *p; p++) {
    if (*p == '{') depth++;
    else if (*p == '}') { depth--; if (depth == 0) return p; }
    else if (*p == '"') {
      p++;
      while (p < lim && *p && *p != '"') { if (*p == '\\' && p + 1 < lim) p++; p++; }
    }
  }
  return nullptr;
}

static bool cqStr(const char* from, const char* to, const char* key,
                  char* dst, size_t dstSz) {
  const char* p = cqKey(from, to, key);
  if (!p || p >= to || *p != '"') return false;
  p++;
  const char* e = p;
  while (e < to && *e && *e != '"') e++;
  if (e >= to || *e != '"') return false;
  size_t n = (size_t)(e - p);
  if (n >= dstSz) n = dstSz - 1;
  memcpy(dst, p, n);
  dst[n] = 0;
  return true;
}

static bool cqNum(const char* from, const char* to, const char* key, float& out) {
  const char* p = cqKey(from, to, key);
  if (!p || p >= to) return false;
  if (*p == 'n' && strncmp(p, "null", 4) == 0) return false;
  char* endp = nullptr;
  float v = strtof(p, &endp);
  if (endp == p) return false;
  out = v;
  return true;
}

static uint32_t cqIsoEpoch(const char* iso) {
  if (!iso || !iso[0]) return 0;
  int Y = 0, M = 0, D = 0, h = 0, m = 0, s = 0;
  if (sscanf(iso, "%d-%d-%dT%d:%d:%d", &Y, &M, &D, &h, &m, &s) < 6) return 0;
  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_year = Y - 1900;
  t.tm_mon = M - 1;
  t.tm_mday = D;
  t.tm_hour = h;
  t.tm_min = m;
  t.tm_sec = s;
#if defined(_GNU_SOURCE) || defined(__linux__) || defined(ESP_PLATFORM) || defined(ARDUINO)
  time_t e = timegm(&t);
#else
  time_t e = mktime(&t);
#endif
  if (e < 0) return 0;
  return (uint32_t)e;
}

// Match the advertised _http._tcp INSTANCE (bonjour-service publishes
// MDNS_NAME as instance). Do not require the machine hostname to be estacao.
// queryHost("estacao") does not create estacao.local.
static bool cotasCiEq(const char *a, const char *b) {
  if (!a || !b) return false;
  while (*a && *b) {
    char ca = *a, cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);
    if (ca != cb) return false;
    a++; b++;
  }
  return *a == 0 && *b == 0;
}

static bool cotasInstanceIsEstacao(const char *instance, const char *want) {
  if (!instance || !instance[0] || !want || !want[0]) return false;
  if (cotasCiEq(instance, want)) return true;
  char dotted[40];
  snprintf(dotted, sizeof(dotted), "%s.local", want);
  return cotasCiEq(instance, dotted);
}

// Identity is the _http._tcp instance (MDNS_NAME / estacao). txtPath is
// optional metadata and must never select a foreign instance.
static bool cotasSelectEstacaoService(const char *instance, const char *want,
                                      const char *txtPath) {
  (void)txtPath;
  return cotasInstanceIsEstacao(instance, want);
}

// Actions MINUTOS / A PAGAR: big number is usedAbsolute (min / USD).
// Cursor on_demand keeps usedPct when both fields are present.
static bool cotasAbsCard(const CotasWindow& w) {
  return w.name[0] && (!strcmp(w.name, "minutos") || !strcmp(w.name, "a_pagar"));
}

static bool cotasShowAbsolute(const CotasWindow& w) {
  return w.hasAbs && (!w.hasPct || cotasAbsCard(w));
}

// Big number on a remote Agora card. Omitted field is never 0%.
static bool cotasFormatBig(const CotasWindow& w, char *dst, size_t n) {
  if (!dst || n == 0) return false;
  dst[0] = 0;
  if ((!w.hasPct && !w.hasAbs) || w.status == COTAS_NOSRC) return false;
  if (cotasShowAbsolute(w)) {
    if (w.unit[0] && !strcmp(w.unit, "usd"))
      snprintf(dst, n, "$%.2f", (double)w.usedAbs);
    else
      snprintf(dst, n, "%d", (int)(w.usedAbs + (w.usedAbs >= 0 ? 0.5f : -0.5f)));
    return true;
  }
  snprintf(dst, n, "%d%%", (int)(w.usedPct + (w.usedPct >= 0 ? 0.5f : -0.5f)));
  return true;
}

static void cqParseWindow(const char* from, const char* to, CotasWindow& w) {
  memset(&w, 0, sizeof(w));
  w.status = COTAS_NOSRC;
  cqStr(from, to, "name", w.name, sizeof(w.name));
  char st[16] = {0};
  if (cqStr(from, to, "status", st, sizeof(st))) w.status = cotasStatusOf(st);
  float v;
  if (cqNum(from, to, "usedPct", v)) { w.hasPct = true; w.usedPct = v; }
  if (cqNum(from, to, "usedAbsolute", v)) { w.hasAbs = true; w.usedAbs = v; }
  cqStr(from, to, "unit", w.unit, sizeof(w.unit));
  char iso[40] = {0};
  if (cqStr(from, to, "resetAt", iso, sizeof(iso))) w.resetEpoch = cqIsoEpoch(iso);
  // Missing usedPct on an otherwise empty window stays no_source.
  if (!w.hasPct && !w.hasAbs) w.status = COTAS_NOSRC;
}

static uint8_t cqParseWindows(const char* from, const char* to, CotasWindow* dst, uint8_t max) {
  const char* k = cqKey(from, to, "windows");
  if (!k) return 0;
  const char* lb = strchr(k, '[');
  if (!lb || lb >= to) return 0;
  uint8_t n = 0;
  const char* p = lb + 1;
  while (n < max && p < to) {
    while (p < to && (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r')) p++;
    if (p >= to || *p == ']') break;
    if (*p != '{') break;
    const char* end = cqMatchBrace(p, to);
    if (!end) break;
    cqParseWindow(p, end + 1, dst[n]);
    n++;
    p = end + 1;
  }
  return n;
}

static bool cqParseSource(const char* from, const char* to, CotasSource& o) {
  memset(&o, 0, sizeof(o));
  if (!cqStr(from, to, "source", o.id, sizeof(o.id))) return false;
  cqStr(from, to, "label", o.label, sizeof(o.label));
  cqStr(from, to, "error", o.error, sizeof(o.error));
  o.nWin = cqParseWindows(from, to, o.win, COTAS_NW);
  o.have = true;
  return true;
}

static bool cotasParse(const char* body, CotasState& out) {
  if (!body || !body[0]) return false;
  const char* end = body + strlen(body);
  const char* arr = cqKey(body, end, "sources");
  if (!arr) {
    // bare array of snapshots
    arr = strchr(body, '[');
    if (!arr) return false;
  }
  const char* lb = strchr(arr, '[');
  if (!lb) return false;

  bool any = false;
  const char* p = lb + 1;
  while (p < end) {
    while (p < end && (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r')) p++;
    if (p >= end || *p == ']') break;
    if (*p != '{') break;
    const char* oe = cqMatchBrace(p, end);
    if (!oe) break;
    CotasSource tmp;
    if (cqParseSource(p, oe + 1, tmp)) {
      int idx = cotasSrcIndex(tmp.id);
      if (idx >= 0) { out.src[idx] = tmp; any = true; }
    }
    p = oe + 1;
  }
  return any;
}
