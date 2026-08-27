#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// Claude Usage Stick — Guition JC4832W535 (ESP32-S3, AXS15231B)
// Pinos: ver firmware/REFERENCIA-HARDWARE-LVGL.md (bring-up validado)
// ============================================================

// ── Firmware ─────────────────────────────────────────────
#define FW_VERSION              "2.3"

// ── Display QSPI (AXS15231B) ─────────────────────────────
#define TFT_CS    45
#define TFT_SCK   47
#define TFT_SDA0  21
#define TFT_SDA1  48
#define TFT_SDA2  40
#define TFT_SDA3  39
#define TFT_BL    1
#define TFT_TE    38

#define SCREEN_WIDTH   480
#define SCREEN_HEIGHT  320
#define QSPI_FREQ      40000000UL

// ── Touch I2C (AXS15231B) ────────────────────────────────
#define TOUCH_SDA  4
#define TOUCH_SCL  8
#define TOUCH_INT  3
#define TOUCH_ADDR 0x3B
// rotation=3 = USB à esquerda (casa com o flush 270° CW)
#define TOUCH_ROTATION 3

// ── Polling ──────────────────────────────────────────────
#define DEFAULT_POLL_SEC        120
#define MIN_POLL_SEC            30
#define MAX_POLL_SEC            300
#define STATUS_POLL_SEC         300      // status.claude.com a cada 5 min

// ── Segurança (PIN + AES-256-GCM) ────────────────────────
#define PIN_LEN                 4
#define MAX_PIN_ATTEMPTS        10
#define LOCKOUT_BASE_SEC        60       // dobra a cada falha
#define KDF_ROUNDS              10000

// ── Watchdog ─────────────────────────────────────────────
// Generoso de proposito: fetchUsage, fetchModelStatus, probeModel e o
// autoConnect do WiFi sao BLOQUEANTES e rodam no loop principal. Somados no
// pior caso passam de 45s. O objetivo aqui e pegar travamento de verdade, nao
// chamada de rede lenta.
#define WDT_TIMEOUT_MS          90000

// ── Rede / API Claude ────────────────────────────────────
#define WIFI_CONNECT_TIMEOUT_MS 8000
#define API_TIMEOUT_MS          15000
// Idade maxima do snapshot da VPS que ainda vale usar. 15 min e o mesmo
// limiar do §7.3 do painel da estacao, onde o dado passa a "atencao".
// Acima disso o pull recusa e cai para a fonte direta — servir dado velho
// como se fosse fresco e pior que nao servir.
#define PULL_IDADE_MAX_S        900
#define MESSAGES_ENDPOINT       "https://api.anthropic.com/v1/messages"
#define ANTHROPIC_VERSION       "2023-06-01"
#define PROBE_MODEL             "claude-haiku-4-5-20251001"
// O token do `claude setup-token` e do Claude Code e normalmente e RECUSADO em
// /v1/messages. A API so devolve os headers unified-* quando a requisicao se
// apresenta como o proprio Claude Code — este User-Agent + o anthropic-beta
// abaixo. Se um dia passarem a exigir versao minima, o sintoma sera 401 sem
// nenhuma pista apontando para cá: e a primeira coisa a conferir.
#define CLAUDE_CODE_UA          "claude-code/2.1.5"
#define ANTHROPIC_OAUTH_BETA    "oauth-2025-04-20"
// status.anthropic.com redireciona para cá — consultar o host canônico direto
// ── Codex Usage Stick (2º provider via bridge na VPS) ────
// URL não é segredo. As credenciais (basic-auth + X-Bridge-Token) vêm em
// runtime pelo onboarding — nunca commitadas.
#define CODEX_USAGE_URL         "https://codex-usage.srv1390429.hstgr.cloud/codex-usage"

// status.anthropic.com redireciona para cá — consultar o host canônico direto
#define STATUS_ENDPOINT         "https://status.claude.com/api/v2/incidents/unresolved.json"

// Brilho noturno: escurece as 21h, volta as 07h. Horarios sao CONSTANTES —
// nao ha tela de ajuste que justifique o custo, e mudar e recompilar.
#define BRI_HORA_NOITE          21
#define BRI_HORA_DIA            7
#define BRI_NOITE               20   // abaixo do menor nivel manual (60)

// NTP (necessário para os contadores de reset)
#define NTP_SERVER_1            "pool.ntp.org"
#define NTP_SERVER_2            "time.cloudflare.com"

// ── NVS ──────────────────────────────────────────────────
#define NVS_NAMESPACE           "claude"

#endif // CONFIG_H
