// Harness de host do parser de /api/github. Roda sem hardware:
//   g++ -std=c++17 -Wall -Wextra -Werror -I../claude_stick github_parse_test.cpp -o /tmp/ghtest
//
// A fixture (github_fixture.json) e o payload REAL servido pela estacao — nao
// um JSON escrito a mao. Foi assim que o equivalente do Codex pegou o bug do
// ']' interno antes de gastar um ciclo de flash, que e caro: nao ha screenshot
// de hardware e abrir a serial reseta o S3.
#include "github_parse.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static int falhas = 0;
static void check(bool cond, const char* msg) {
    printf("  %s %s\n", cond ? "ok  " : "FALHA", msg);
    if (!cond) falhas++;
}

static char* ler(const char* caminho) {
    FILE* f = fopen(caminho, "rb");
    if (!f) { printf("nao abriu %s\n", caminho); return nullptr; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* b = (char*)malloc((size_t)n + 1);
    size_t lidos = fread(b, 1, (size_t)n, f);
    b[lidos] = 0;
    fclose(f);
    return b;
}

int main() {
    char* body = ler("github_fixture.json");
    if (!body) return 1;

    GithubUsage g;
    printf("\n[1] parse do payload real\n");
    check(ghParse(body, g), "ghParse devolve true");
    check(g.ok, "ok");
    check(g.billing, "fonte == billing (a fixture veio com fatura)");

    printf("\n[2] cota e custo — os numeros grandes da tela\n");
    check(g.usadosMin == 8559, "usados_min 8559");
    check(g.incluidosMin == 2240, "incluidos_min 2240 (da fatura, nao 2000 assumidos)");
    check(g.pagosMin == 6319, "pagos_min 6319");
    check(g.pct == 382, "pct 382 — passa de 100 e NAO satura");
    check(fabs(g.usd - 37.91) < 0.005, "usd 37.91 (da fatura)");
    check(fabs(g.limiteUsd - 100.0) < 0.01, "limite 100");
    check(g.pctLimite == 38, "38% do limite");

    printf("\n[3] rankings\n");
    check(g.nProj == 3, "3 projetos");
    check(strcmp(g.proj[0].key, "Totvs-Moda-CRM") == 0, "1o projeto e o CRM");
    check(g.nJob == 4, "4 linhas de job");
    check(strcmp(g.job[0].key, "Test & Build") == 0, "job lider e Test & Build");
    check(g.job[0].pct >= 70, "e ele domina o ranking");

    printf("\n[4] a armadilha do ']' interno — o v[] dentro de cada dia\n");
    check(g.nDay == 13, "13 dias parseados, nao 1");
    check(g.nProjOrder == 3, "proj_order com 3 entradas");
    check(strcmp(g.day[0].label, "07-18") == 0, "primeiro dia 07-18");
    check(strcmp(g.day[g.nDay - 1].label, "07-31") == 0, "ultimo dia 07-31");
    // ultimo dia da fixture: {"d":"07-31","min":1262,"v":[1014,244,4]}
    check(g.day[g.nDay - 1].min == 1262, "min do ultimo dia");
    check(g.day[g.nDay - 1].v[0] == 1014 && g.day[g.nDay - 1].v[1] == 244
          && g.day[g.nDay - 1].v[2] == 4, "v[] do ultimo dia inteiro");

    printf("\n[5] cada fatia soma o total do dia\n");
    int ruins = 0;
    for (uint8_t i = 0; i < g.nDay; i++) {
        uint32_t s = 0;
        for (uint8_t k = 0; k < g.nProjOrder; k++) s += g.day[i].v[k];
        if (s != g.day[i].min) { printf("      dia %s: v soma %u, min %u\n",
                                        g.day[i].label, s, g.day[i].min); ruins++; }
    }
    check(ruins == 0, "todo dia fecha");

    printf("\n[6] saude do CI (janela de 7 dias)\n");
    check(g.hasCi, "tem CI");
    check(g.ciRuns > 0 && g.ciPctFalha <= 100, "runs e pct plausiveis");

    printf("\n[7] entradas degeneradas nao viram numero plausivel\n");
    GithubUsage z;
    check(!ghParse("", z), "corpo vazio -> false");
    check(!ghParse("{}", z), "json sem cota -> false");
    check(!ghParse("nao e json", z), "lixo -> false");
    check(ghParse("{\"fonte\":\"calculado\",\"cota\":{\"usados_min\":0,"
                  "\"incluidos_min\":2000,\"pagos_min\":0,\"pct\":0}}", z),
          "payload minimo valido -> true");
    check(!z.billing, "fonte calculado -> billing false");
    check(z.nDay == 0 && z.nProj == 0, "arrays ausentes ficam vazios, nao lixo");

    printf("\n[8] truncamento nao estoura buffer\n");
    // nome de job maior que GhItem.key
    check(ghParse("{\"cota\":{\"usados_min\":1,\"incluidos_min\":1,\"pagos_min\":0,\"pct\":1},"
                  "\"jobs\":[{\"nome\":\"um-nome-de-job-absurdamente-longo-que-nao-cabe\","
                  "\"min\":1,\"pct\":1}]}", z), "parse com nome longo");
    check(strlen(z.job[0].key) < sizeof(z.job[0].key), "nome truncado com terminador");

    free(body);
    printf(falhas ? "\nFALHOU: %d\n" : "\ntodos os testes passaram\n", falhas);
    return falhas ? 1 : 0;
}
