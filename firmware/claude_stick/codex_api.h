#pragma once
#include <stdint.h>

// Uso do Codex/ChatGPT, obtido do bridge na VPS do Hermes (ZYN-384).
// O device NÃO fala com chatgpt.com (Cloudflare) — só com o nosso endpoint,
// que já entrega o JSON pronto. Sem token OAuth no device, sem refresh.
//
// Resposta do bridge (GET /codex-usage):
//   { "ok":true, "plan":"plus", "allowed":true, "limit_reached":false,
//     "h5": null | { "used_percent":.., "reset_after":.., "reset_at":.. },
//     "d7": { ... }, "ts": .. }
// h5/d7 podem vir null (janela sem uso recente) → has5h/has7d = false.
struct CodexUsage {
    bool     ok;              // fetch + parse deram certo
    bool     allowed;         // rate_limit.allowed
    bool     limitReached;    // rate_limit.limit_reached
    char     plan[12];        // "plus" | "pro" | ...

    bool     has5h;           // janela de 5h presente (não-null)
    float    pct5;            // % usado da janela 5h
    uint32_t reset5Epoch;     // unix ts do reset 5h
    uint32_t after5;          // segundos até o reset 5h (countdown pronto)

    bool     has7d;
    float    pct7;
    uint32_t reset7Epoch;
    uint32_t after7;

    bool     stale;           // bridge devolveu último-bom (ok:false do bridge)
    char     error[48];       // motivo quando ok=false
};

// GET no bridge com os dois portões: basic-auth (base64 de user:senha) no
// header Authorization, e o segredo do bridge em X-Bridge-Token.
bool fetchCodexUsage(const char* url, const char* basicAuthB64,
                     const char* bridgeToken, CodexUsage& out);
