#include "cotas.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include <Preferences.h>

static CotasState g_cotas = {};
// Poll runs only on loopTask. Keep the transactional parse buffer off its
// small stack: provider fields made this structure exceed 5 KB in v2.10.
static CotasState g_nextCotas = {};
static uint32_t g_lastTryMs = 0;
static IPAddress g_ip;
static uint16_t g_port = ESTACAO_PORT;

// Ultimo alvo PROVADO (GET 200 + parse ok), persistido em NVS: sobrevive a
// reboot e a regravacao. E o que faltou nas 28h de 16-17/09 — ver o
// comentario de cotasEscolherAlvo em cotas_parse.h.
static uint32_t g_nvsIp = 0;
static uint16_t g_nvsPort = 0;
static bool g_nvsLoaded = false;

static uint32_t ip2u32(const IPAddress &a) {
  return ((uint32_t)a[0] << 24) | ((uint32_t)a[1] << 16) | ((uint32_t)a[2] << 8) | a[3];
}
static IPAddress u32ip(uint32_t v) {
  return IPAddress((v >> 24) & 255, (v >> 16) & 255, (v >> 8) & 255, v & 255);
}

static void alvo_nvs_load() {
  if (g_nvsLoaded) return;
  Preferences p;
  if (p.begin("cotas", true)) {          // read-only falha se o namespace nao existe: ok
    g_nvsIp = p.getUInt("ip", 0);
    g_nvsPort = p.getUShort("port", 0);
    p.end();
  }
  g_nvsLoaded = true;
}

static void alvo_nvs_save(uint32_t ip, uint16_t port) {
  if (ip == g_nvsIp && port == g_nvsPort) return;   // NVS tem ciclos finitos
  Preferences p;
  if (!p.begin("cotas", false)) return;
  p.putUInt("ip", ip);
  p.putUShort("port", port);
  p.end();
  g_nvsIp = ip; g_nvsPort = port;
}

CotasState& cotasState() { return g_cotas; }

const CotasSource* cotasSource(int idx) {
  if (idx < 0 || idx >= COTAS_NSRC) return nullptr;
  return g_cotas.src[idx].have ? &g_cotas.src[idx] : nullptr;
}

bool cotasDue(uint32_t nowMs) {
  if (g_lastTryMs == 0) return true;
  return nowMs - g_lastTryMs >= (uint32_t)COTAS_POLL_SEC * 1000UL;
}

bool cotasIsStale(uint32_t nowMs) {
  if (g_cotas.atMs == 0) return !g_cotas.stationUp;
  return nowMs - g_cotas.atMs > (uint32_t)COTAS_POLL_SEC * 1000UL * COTAS_STALE_MULT;
}

void cotasMarkStationDown() { g_cotas.stationUp = false; }

static bool resolve_estacao() {
  bool mdnsOk = false;
  IPAddress mip; uint16_t mport = 0;
  int n = MDNS.queryService("http", "tcp");
  for (int i = 0; i < n; i++) {
    // Instance name is what bonjour-service publishes as MDNS_NAME.
    // hostname(i) is the machine — do not require it to start with estacao.
    String inst = MDNS.instanceName(i);
    if (!cotasSelectEstacaoService(inst.c_str(), ESTACAO_MDNS_HOST, nullptr))
      continue;
    mip = MDNS.address(i);
    mport = MDNS.port(i);
    if (mip[0] != 0) { mdnsOk = true; break; }
  }
  // queryHost("estacao") does not create estacao.local — skip it.

  alvo_nvs_load();
  uint32_t cfgIp = 0;
  cotasParseIpv4(ESTACAO_FALLBACK_IP, &cfgIp);   // "" simplesmente nao parseia

  CotasAlvo alvo;
  if (!cotasEscolherAlvo(mdnsOk, mdnsOk ? ip2u32(mip) : 0, mport,
                         ip2u32(g_ip), g_port,
                         g_nvsIp, g_nvsPort,
                         cfgIp, ESTACAO_PORT,
                         ESTACAO_PORT, &alvo))
    return false;
  g_ip = u32ip(alvo.ip);
  g_port = alvo.port;
  return true;
}

bool cotasPoll() {
  g_lastTryMs = millis();
  if (!WiFi.isConnected()) {
    g_cotas.stationUp = false;
    return false;
  }
  if (!resolve_estacao()) {
    Serial.println("[cotas] estacao nao encontrada via mDNS");
    g_cotas.stationUp = false;
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  String url = "http://" + g_ip.toString() + ":" + String(g_port) + "/cotas";
  Serial.printf("[cotas] GET %s\n", url.c_str());
  if (!http.begin(client, url)) {
    g_cotas.stationUp = false;
    return false;
  }
  http.setTimeout(COTAS_TIMEOUT_MS);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[cotas] HTTP %d\n", code);
    g_cotas.stationUp = false;
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  g_nextCotas = g_cotas;
  if (!cotasParse(body.c_str(), g_nextCotas)) {
    Serial.println("[cotas] parse fail");
    g_cotas.stationUp = false;
    return false;
  }
  g_nextCotas.stationUp = true;
  g_nextCotas.atMs = millis();
  g_cotas = g_nextCotas;
  alvo_nvs_save(ip2u32(g_ip), g_port);   // persiste so alvo PROVADO
  Serial.printf("[cotas] stack minimum free=%u bytes\n", (unsigned)uxTaskGetStackHighWaterMark(nullptr));
  Serial.printf("[cotas] ok  cursor have=%d  grok_win=%d  actions have=%d\n",
                (int)g_cotas.src[2].have,
                (int)(g_cotas.src[2].nWin > 2 && g_cotas.src[2].win[2].hasPct),
                (int)g_cotas.src[3].have);
  return true;
}
