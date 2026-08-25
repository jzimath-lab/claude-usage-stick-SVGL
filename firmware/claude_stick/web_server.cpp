#include "web_server.h"
#include "ui_refresh.h"
#include "logo_assets.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "certs.h"
#include "cotas_parse.h"
#include "storage.h"

void nav_cb(lv_event_t *e);   // navegacao generica, segue no claude_stick.ino

// ============================================================
// WebServer: token (onboarding) + dados (bridge de tokens)
// ============================================================
static WebServer *g_web = nullptr;
static volatile bool g_tokenGot = false;
static lv_obj_t *g_tokMsg = nullptr;            // status na tela do device

void stop_web() { if (g_web) { g_web->stop(); delete g_web; g_web = nullptr; } }

static bool g_mdnsUp = false;
void ensure_mdns() {
  if (g_mdnsUp || !g_wifi.isConnected()) return;
  if (MDNS.begin("claude-stick")) {
    MDNS.addService("http", "tcp", 80);
    g_mdnsUp = true;
    Serial.println("[MDNS] claude-stick.local");
  }
}

void anim_opa_cb(void *o, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)o, (lv_opa_t)v, 0); }

// Clawd oficial (pixel-art) com "respiração" — substitui o antigo sol de raios.
lv_obj_t *build_claude_mark(lv_obj_t *parent) {
  lv_obj_t *img = lv_image_create(parent);
  lv_image_set_src(img, &img_clawd_big);
  lv_anim_t a; lv_anim_init(&a);
  lv_anim_set_var(&a, img);
  lv_anim_set_exec_cb(&a, anim_opa_cb);
  lv_anim_set_values(&a, 140, 255);
  lv_anim_set_duration(&a, 900);
  lv_anim_set_playback_duration(&a, 900);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_start(&a);
  return img;
}

// ---- páginas HTML ----
#define WEB_CSS \
  ":root{--bg:#0F0F12;--card:#1A1A20;--bd:#30303A;--tx:#F2F0EC;--mut:#8C8C98;--cor:#D97757}" \
  "*{box-sizing:border-box}" \
  "body{margin:0;background:var(--bg);color:var(--tx);font-family:-apple-system,Segoe UI,Roboto,sans-serif;" \
  "display:flex;min-height:100vh;align-items:center;justify-content:center}" \
  ".card{background:var(--card);border:1px solid var(--bd);border-radius:16px;padding:26px;max-width:520px;width:92%}" \
  "h1{font-size:19px;margin:0 0 6px;display:flex;align-items:center;gap:10px}" \
  "p{color:var(--mut);font-size:14px;line-height:1.5;margin:6px 0 14px}" \
  "textarea{width:100%;background:var(--bg);color:var(--tx);border:1px solid var(--bd);border-radius:10px;" \
  "padding:12px;font-family:ui-monospace,monospace;font-size:13px;min-height:96px;resize:vertical}" \
  "button{margin-top:14px;width:100%;background:var(--cor);color:#1A1A20;border:0;border-radius:10px;" \
  "padding:14px;font-size:16px;font-weight:700;cursor:pointer}" \
  ".spark{width:26px;height:26px;flex:0 0 auto}code,a{color:var(--cor)}"

#define WEB_SPARK \
  "<svg class=spark viewBox='0 0 100 100'><g stroke='#D97757' stroke-width='12' stroke-linecap='round'>" \
  "<line x1=50 y1=9 x2=50 y2=91/><line x1=9 y1=50 x2=91 y2=50/>" \
  "<line x1=21 y1=21 x2=79 y2=79/><line x1=79 y1=21 x2=21 y2=79/>" \
  "<line x1=34 y1=11 x2=66 y2=89/><line x1=66 y1=11 x2=34 y2=89/></g></svg>"

String web_form() {
  String h = F("<!doctype html><html lang=pt><head><meta charset=utf-8>"
               "<meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Claude Usage Stick</title><style>" WEB_CSS "</style></head><body><div class=card>"
               "<h1>" WEB_SPARK " Claude Usage Stick</h1>"
               "<p>Cole o seu token OAuth do Claude (<code>sk-ant-oat01-...</code>) e toque em <b>Salvar</b>. "
               "O gadget vai <b>validar</b> o token e pedir um PIN na tela.</p>"
               "<form method=POST action='/token'>"
               "<textarea name=token placeholder='sk-ant-oat01-...' autocomplete=off autofocus></textarea>"
               "<button type=submit>Salvar e validar</button></form></div></body></html>");
  return h;
}
String web_result(bool ok, const String &msg) {
  String h = F("<!doctype html><html lang=pt><head><meta charset=utf-8>"
               "<meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Claude Usage Stick</title><style>" WEB_CSS "</style></head><body><div class=card>");
  if (ok) {
    // ⚠️ O ramo de sucesso IGNORAVA o `msg` e mandava sempre "defina um PIN" —
    // instrucao correta para o token da Anthropic e ERRADA para quem acabou de
    // configurar a VPS, que ja tem PIN definido. Uma tela de sucesso que manda
    // fazer a coisa errada e pior que uma sem texto: ela parece autoridade.
    if (msg.length()) {
      h += F("<h1>" WEB_SPARK " Pronto</h1><p>");
      h += msg;
      h += F("</p><p>Pode fechar esta página.</p>");
    } else {
      h += F("<h1>" WEB_SPARK " Token validado</h1>"
             "<p>Token aceito pela API. Agora <b>defina um PIN de 4 dígitos</b> na tela do gadget para finalizar. "
             "Pode fechar esta página.</p>");
    }
  } else {
    h += F("<h1>" WEB_SPARK " Token recusado</h1><p>");
    h += msg;
    h += F("</p><p><a href='/'>Voltar e tentar de novo</a></p>");
  }
  h += F("</div></body></html>");
  return h;
}

void handleRoot()     { g_web->send(200, "text/html; charset=utf-8", web_form()); }
void handleNotFound() { g_web->sendHeader("Location", "/"); g_web->send(302, "text/plain", ""); }

void handleTokenPost() {
  String t = g_web->arg("token");
  t.trim();
  if (t.length() < 8) {
    if (g_tokMsg) lv_label_set_text(g_tokMsg, TRS("token vazio", "empty token"));
    g_web->send(200, "text/html; charset=utf-8", web_result(false, "Token vazio ou muito curto."));
    return;
  }
  // feedback no device antes da chamada bloqueante
  if (g_tokMsg) { lv_label_set_text(g_tokMsg, TRS("validando token...", "validating token...")); lv_refr_now(NULL); }

  UsageData tmp = {};
  bool ok = fetchUsage(t.c_str(), tmp);
  if (ok) {
    strlcpy(g_pendingToken, t.c_str(), sizeof(g_pendingToken));
    g_usage = tmp;                              // já temos dados p/ o dashboard
    g_pinConfirming = false; g_pinFirst[0] = 0; g_pinEntry[0] = 0;
    g_tokenGot = true;                          // loop -> ST_SETUP_PIN
    if (g_tokMsg) lv_label_set_text(g_tokMsg, TRS("token OK! defina o PIN", "token OK! set the PIN"));
    g_web->send(200, "text/html; charset=utf-8", web_result(true, ""));
  } else {
    String m = String("A API recusou o token (") + tmp.error + "). Confira e cole de novo.";
    if (g_tokMsg) lv_label_set_text(g_tokMsg, TRS("token recusado, tente de novo", "token rejected, try again"));
    g_web->send(200, "text/html; charset=utf-8", web_result(false, m));
  }
}

// ---- Endpoints de dados (bridge de tokens; ver tools/token_bridge.py) ----
long long jll(const String &s, const char *key) {
  String k = String("\"") + key + "\"";
  int i = s.indexOf(k); if (i < 0) return 0;
  i = s.indexOf(':', i + k.length() - 1); if (i < 0) return 0;
  return atoll(s.c_str() + i + 1);
}
void handleWindow() {
  char b[192];
  snprintf(b, sizeof(b),
           "{\"now\":%lu,\"h5_reset\":%lu,\"d7_reset\":%lu,\"h5_util\":%.4f,\"d7_util\":%.4f}",
           (unsigned long)time(nullptr),
           (unsigned long)g_usage.h5ResetEpoch, (unsigned long)g_usage.d7ResetEpoch,
           g_usage.h5 / 100.0f, g_usage.d7 / 100.0f);
  g_web->send(200, "application/json", b);
}
void handleTokensPost() {
  String body = g_web->arg("plain");
  g_tok.tin      = jll(body, "in");
  g_tok.tout     = jll(body, "out");
  g_tok.cache    = jll(body, "cache");
  g_tok.sessions = (int)jll(body, "sessions");
  g_tok.atMs     = millis();
  Serial.printf("[TOK] in=%lld out=%lld cache=%lld sess=%d\n",
                g_tok.tin, g_tok.tout, g_tok.cache, g_tok.sessions);
  g_web->send(200, "application/json", "{\"ok\":true}");
  update_tok_row();
}
void handleInfo() {
  String h = F("<!doctype html><html lang=pt><head><meta charset=utf-8>"
               "<meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Claude Usage Stick</title><style>" WEB_CSS "</style></head><body><div class=card>"
               "<h1>" WEB_SPARK " Claude Usage Stick</h1>"
               "<p>Device online. Endpoints: <code>GET /window</code> (janela atual) e "
               "<code>POST /tokens</code> (bridge de tokens por sessao — ver tools/token_bridge.py).</p>"
               "</div></body></html>");
  g_web->send(200, "text/html; charset=utf-8", h);
}

// ---- credenciais da VPS da estacao ----------------------------------------
//
// Fluxo SEPARADO do /token de propósito. Aquele valida chamando fetchUsage(),
// isto e, perguntando a ANTHROPIC se o segredo presta — correto para o token
// dela e inutil para qualquer outro. Este valida contra a PROPRIA VPS, com um
// GET real ao /api/display.
String web_form_vps() {
  String h = F("<!doctype html><html lang=pt><head><meta charset=utf-8>"
               "<meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Cotas pela estacao</title><style>" WEB_CSS "</style></head><body><div class=card>"
               "<h1>" WEB_SPARK " Cotas pela estacao</h1>"
               "<p>Com estas credenciais o gadget passa a ler as cotas do servidor da "
               "estacao, em vez de falar direto com a Anthropic.</p>"
               "<form method=POST action='/vps'>"
               "<input name=host placeholder='estacao-display.exemplo.cloud' autocomplete=off autofocus>"
               "<input name=token placeholder='X-Device-Token' autocomplete=off>"
               "<input name=user placeholder='usuario (basicAuth, opcional)' autocomplete=off>"
               "<input name=pass type=password placeholder='senha (basicAuth, opcional)' autocomplete=off>"
               "<button type=submit>Salvar e validar</button></form>");
  if (g_vps.host[0]) {
    h += F("<p>Configurado agora: <code>");
    h += g_vps.host;
    h += F("</code></p>");
  }
  h += F("</div></body></html>");
  return h;
}

void handleVpsGet() { g_web->send(200, "text/html; charset=utf-8", web_form_vps()); }

void handleVpsPost() {
  VpsCreds nova = {};
  strlcpy(nova.host,  g_web->arg("host").c_str(),  sizeof(nova.host));
  strlcpy(nova.user,  g_web->arg("user").c_str(),  sizeof(nova.user));
  strlcpy(nova.pass,  g_web->arg("pass").c_str(),  sizeof(nova.pass));
  strlcpy(nova.token, g_web->arg("token").c_str(), sizeof(nova.token));

  // ⚠️ Valida a FORMA antes de gastar uma requisicao: host com esquema colado
  // ("https://...") e o erro de digitacao mais provavel, e sem esta checagem a
  // URL viraria "https://https://..." e o erro de rede nao apontaria a causa.
  char url[160];
  if (!vpsCredsValidas(nova) || !vpsUrlDisplay(nova.host, url, sizeof(url))) {
    g_web->send(200, "text/html; charset=utf-8",
                web_result(false, "Informe o host (so o nome, sem https://) e o device token."));
    return;
  }

  // Validacao de verdade: um GET ao /api/display com as credenciais. Aceitar
  // sem provar deixaria o operador achando que configurou, e o defeito so
  // apareceria como coluna vazia, sem dizer por que.
  WiFiClientSecure client;
  client.setCACert(CA_BUNDLE);
  HTTPClient https;
  if (!https.begin(client, url)) {
    g_web->send(200, "text/html; charset=utf-8", web_result(false, "Nao consegui abrir a conexao."));
    return;
  }
  https.addHeader("X-Device-Token", nova.token);
  if (nova.user[0]) {
    char basic[192];
    if (vpsAuthBasic(nova.user, nova.pass, basic, sizeof(basic)))
      https.addHeader("Authorization", basic);
  }
  https.setTimeout(API_TIMEOUT_MS);
  int code = https.GET();
  String corpo = (code == 200) ? https.getString() : String();
  https.end();

  Serial.printf("[VPS] validacao %s -> HTTP %d\n", url, code);

  if (code != 200) {
    // Distingue as causas em vez de dizer "falhou": 401 com basicAuth vazio e
    // um caso comum e tem remedio diferente de token errado.
    // if/else e nao ternario: misturar F() com concatenacao num ?: da tipos
    // incompativeis (__FlashStringHelper* x StringSumHelper).
    String m;
    if (code == 401)      m = F("HTTP 401: device token ou basicAuth recusado pela borda.");
    else if (code == 404) m = F("HTTP 404: host responde, mas nao serve /api/display.");
    else                { m = F("HTTP "); m += code; m += F(" na validacao."); }
    g_web->send(200, "text/html; charset=utf-8", web_result(false, m));
    return;
  }

  // 200 nao basta: o payload precisa conter cotas que o parser entenda. Um
  // proxy amigavel devolvendo 200 com HTML passaria no teste de status.
  CotasVps c;
  if (!parseCotas(corpo.c_str(), c)) {
    g_web->send(200, "text/html; charset=utf-8",
                web_result(false, "Respondeu 200, mas sem bloco de cotas reconhecivel."));
    return;
  }

  g_vps = nova;
  save_vps();
  char msg[96];
  snprintf(msg, sizeof(msg), "Validado: %u provedores, idade %ds.", (unsigned)c.nProv, (int)c.idadeS);
  g_web->send(200, "text/html; charset=utf-8", web_result(true, msg));
}

void start_data_web() {
  stop_web();
  ensure_mdns();
  g_web = new WebServer(80);
  g_web->on("/", HTTP_GET, handleInfo);
  g_web->on("/window", HTTP_GET, handleWindow);
  g_web->on("/tokens", HTTP_POST, handleTokensPost);
  g_web->on("/vps", HTTP_GET,  handleVpsGet);
  g_web->on("/vps", HTTP_POST, handleVpsPost);
  g_web->onNotFound([]() { g_web->send(404, "application/json", "{\"error\":\"not_found\"}"); });
  g_web->begin();
}

void ui_token() {
  stop_web();
  lv_obj_t *scr = lv_screen_active();

  if (!g_onboarding && g_hasToken) {
    lv_obj_t *bk = mkbtn(scr, TRS(LV_SYMBOL_LEFT " Voltar", LV_SYMBOL_LEFT " Back"),
                         &lv_font_montserrat_14, C_SURFACE2, C_MUTED);
    lv_obj_align(bk, LV_ALIGN_TOP_RIGHT, -12, 8);
    lv_obj_add_event_cb(bk, nav_cb, LV_EVENT_CLICKED, (void *)(intptr_t)(g_usage.ok ? ST_MAIN : ST_SETTINGS));
  }

  lv_obj_t *mark = build_claude_mark(scr);
  lv_obj_align(mark, LV_ALIGN_TOP_MID, 0, 8);

  lv_obj_t *cap = mklabel(scr, TRS("Cole o token pelo navegador, em:",
                                   "Paste the token via browser, at:"), &lv_font_montserrat_16, C_MUTED);
  lv_obj_align(cap, LV_ALIGN_TOP_MID, 0, 108);

  String url = String("http://") + WiFi.localIP().toString();
  lv_obj_t *ip = mklabel(scr, url.c_str(), &lv_font_montserrat_28, C_ACCENT);
  lv_obj_align(ip, LV_ALIGN_TOP_MID, 0, 132);

  lv_obj_t *hint = mklabel(scr, TRS("abra esse endereco no PC/celular na MESMA rede WiFi",
                                    "open this address on a PC/phone on the SAME WiFi"),
                           &lv_font_montserrat_12, C_MUTED);
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 174);

  lv_obj_t *sp = lv_spinner_create(scr);
  lv_spinner_set_anim_params(sp, 1200, 70);
  lv_obj_set_size(sp, 36, 36);
  lv_obj_align(sp, LV_ALIGN_BOTTOM_MID, 0, -46);
  lv_obj_set_style_arc_color(sp, lv_color_hex(C_SURFACE2), LV_PART_MAIN);
  lv_obj_set_style_arc_color(sp, lv_color_hex(C_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_arc_width(sp, 5, LV_PART_MAIN);
  lv_obj_set_style_arc_width(sp, 5, LV_PART_INDICATOR);

  g_tokMsg = mklabel(scr, TRS("aguardando o token...", "waiting for the token..."), &lv_font_montserrat_14, C_MUTED);
  lv_obj_align(g_tokMsg, LV_ALIGN_BOTTOM_MID, 0, -14);

  // sobe o servidor web (formulario do token)
  g_web = new WebServer(80);
  g_web->on("/", HTTP_GET, handleRoot);
  g_web->on("/token", HTTP_POST, handleTokenPost);
  g_web->onNotFound(handleNotFound);
  g_web->begin();
  Serial.printf("[WEB] servidor em %s\n", url.c_str());
}


// --- API para o loop principal ---
// g_web e g_tokenGot ficam privados: o loop so precisa "bombear" o servidor
// e saber se o token chegou pelo formulario.
void web_pump() { if (g_web) g_web->handleClient(); }

bool web_token_arrived() {
  if (!g_web || !g_tokenGot) return false;
  g_tokenGot = false;
  return true;
}

// Zera os ponteiros de UI proprios deste modulo (ver render_state).
void ui_token_invalidate() { g_tokMsg = nullptr; }
