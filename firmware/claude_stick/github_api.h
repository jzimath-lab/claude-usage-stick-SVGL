#pragma once
#include <stdint.h>

// Consumo de GitHub Actions, servido pela estacao (tela de 7") em /api/github.
// O device NAO fala com a api.github.com: quem coleta, agrega e guarda o PAT e
// o servidor. Aqui so chega JSON pronto — sem token do GitHub no firmware.
//
// Tres janelas DIFERENTES convivem no payload, e cada tela tem que dizer a sua:
//   cota/custo/projetos/jobs -> ciclo de faturamento (reseta 00:00 UTC do dia 1o)
//   diario                   -> 14 dias
//   ci                       -> 7 dias
#define GH_ROWS 5    // linhas de ranking (projetos / jobs)
#define GH_DAYS 14   // colunas do grafico diario
#define GH_PROJ 5    // fatias empilhadas por dia (= proj_order do servidor)

struct GhItem { char key[20]; uint32_t min; uint8_t pct; float usd; };
struct GhDay  { char label[6]; uint32_t min; uint16_t v[GH_PROJ]; };

struct GithubUsage {
    bool     ok;               // fetch + parse deram certo
    bool     billing;          // fonte=="billing" (fatura); false = estimado
    char     error[48];

    // --- ciclo de faturamento ---
    uint32_t usadosMin;
    uint32_t incluidosMin;     // cota gratuita REAL (vem da fatura, nao assumida)
    uint32_t pagosMin;
    uint16_t pct;              // pode passar de 100 — medido 382%
    float    usd;              // liquido a pagar; 0 enquanto a cota nao acaba
    float    limiteUsd;        // 0 = sem limite configurado
    uint8_t  pctLimite;
    uint8_t  diasRestantes;

    uint8_t  nProj;  GhItem proj[GH_ROWS];
    uint8_t  nJob;   GhItem job[GH_ROWS];

    // --- 14 dias ---
    uint8_t  nProjOrder; char projOrder[GH_PROJ][20];
    uint8_t  nDay;   GhDay day[GH_DAYS];

    // --- 7 dias: pior repo do CI, que e o que cabe no rodape ---
    bool     hasCi;
    char     ciRepo[20];
    uint16_t ciRuns, ciFalhas;
    uint8_t  ciPctFalha;
    uint32_t ciMinPerdidos;

    float    divergenciaPct;   // <0 = ausente (sem fatura para comparar)
    uint32_t ts;
};

// GET com os dois portoes: basic-auth (base64 de user:senha) no header
// Authorization, e o segredo do device em X-Device-Token — header proprio
// porque o basic auth da borda OCUPA o Authorization.
bool fetchGithubUsage(const char* url, const char* basicAuthB64,
                      const char* deviceToken, GithubUsage& out);
