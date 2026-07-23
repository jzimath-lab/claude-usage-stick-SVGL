/**
 * web_server.h — servidor web de onboarding (token) e bridge de tokens + tela de token
 *
 * Extraido do claude_stick.ino na quebra do monolito (ZYN-379).
 */
#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "app_state.h"
#include "ui_helpers.h"

void stop_web();
void ensure_mdns();
void anim_opa_cb(void *o, int32_t v);
lv_obj_t *build_claude_mark(lv_obj_t *parent);
String web_form();
String web_result(bool ok, const String &msg);
void handleRoot();
void handleNotFound();
void handleTokenPost();
long long jll(const String &s, const char *key);
void handleWindow();
void handleTokensPost();
void handleInfo();
void start_data_web();
void ui_token();

void web_pump();             // handleClient() se o servidor estiver de pe
bool web_token_arrived();    // consome o flag de token recebido (one-shot)
void ui_token_invalidate();  // zera os ponteiros de UI deste modulo

#endif // WEB_SERVER_H
