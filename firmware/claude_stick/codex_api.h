#pragma once
#include <stdint.h>

// Analytics do Codex Cloud (mesma fonte da pagina chatgpt.com/codex/cloud/settings/analytics),
// servida pelo bridge no campo "an". Alimenta as telas Origem / Modelo / Interacoes.
#define CXAN_ROWS 5      // top-N origens/modelos exibidos
#define CXAN_DAYS 14     // dias no mini-historico diario
#define CXAN_SURF 5      // origens no gráfico diário empilhado (= surf_order)

struct CxAnItem { char key[14]; float val; uint8_t pct; };          // origem (credits) ou modelo (turns)
// dia do gráfico empilhado: total + créditos por origem (v[] na ordem de surfOrder)
struct CxAnDay  { char label[6]; uint16_t credits; uint16_t turns; uint16_t v[CXAN_SURF]; };

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
    bool     anStale;         // analytics datadas: bridge em erro, ou preservadas do ciclo anterior
    char     error[48];       // motivo quando ok=false

    // ---- Analytics (seção "an" do bridge; ausente em bridges antigos) ----
    bool     hasAn;           // seção "an" presente e parseada
    uint16_t anRangeDays;     // janela do agregado (ex.: 30)
    uint32_t interactions;    // total de turns no período
    uint32_t anThreads;       // total de threads
    float    creditsTotal;    // créditos consumidos no período
    uint8_t  nSurface;        // itens em surface[]
    CxAnItem surface[CXAN_ROWS];   // origem do consumo (credits + %)
    uint8_t  nModel;          // itens em model[]
    CxAnItem model[CXAN_ROWS];     // modelo consumido (turns + %)
    uint8_t  nSurfOrder;      // itens em surfOrder[]
    char     surfOrder[CXAN_SURF][14];  // ordem canônica das origens (cores consistentes)
    uint8_t  nDay;            // itens em day[]
    CxAnDay  day[CXAN_DAYS];  // consumo diário (últimos N dias)
};

// GET no bridge com os dois portões: basic-auth (base64 de user:senha) no
// header Authorization, e o segredo do bridge em X-Bridge-Token.
bool fetchCodexUsage(const char* url, const char* basicAuthB64,
                     const char* bridgeToken, CodexUsage& out);
