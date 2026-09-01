#include "cotas.h"
#include "config.h"
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>

static CotasState g_cotas = {};
static uint32_t g_lastTryMs = 0;
static IPAddress g_ip;
static uint16_t g_port = ESTACAO_PORT;

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
  int n = MDNS.queryService("http", "tcp");
  for (int i = 0; i < n; i++) {
    String host = MDNS.hostname(i);
    host.toLowerCase();
    if (host.startsWith(ESTACAO_MDNS_HOST)) {
      g_ip = MDNS.IP(i);
      g_port = MDNS.port(i);
      if (g_port == 0) g_port = ESTACAO_PORT;
      return g_ip[0] != 0;
    }
  }
  IPAddress ip = MDNS.queryHost(ESTACAO_MDNS_HOST);
  if (ip && ip[0] != 0) {
    g_ip = ip;
    g_port = ESTACAO_PORT;
    return true;
  }
  return g_ip[0] != 0;   // last known
}

bool cotasPoll() {
  g_lastTryMs = millis();
  if (!WiFi.isConnected()) {
    g_cotas.stationUp = false;
    return false;
  }
  if (!resolve_estacao()) {
    g_cotas.stationUp = false;
    return false;
  }

  WiFiClient client;
  HTTPClient http;
  String url = "http://" + g_ip.toString() + ":" + String(g_port) + "/cotas";
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

  CotasState next = g_cotas;
  if (!cotasParse(body.c_str(), next)) {
    Serial.println("[cotas] parse fail");
    g_cotas.stationUp = false;
    return false;
  }
  next.stationUp = true;
  next.atMs = millis();
  g_cotas = next;
  Serial.printf("[cotas] ok  actions have=%d\n", (int)g_cotas.src[3].have);
  return true;
}
