// Teste de host do parser do bridge do Codex. Compila com g++ e roda contra um
// fixture real (fixture.json) — cobre o MESMO codex_parse.h que roda no device.
//
// Uso:   c++ -std=c++17 -Wall -o parse_test parse_test.cpp && ./parse_test fixture.json
// Roda no CI (.github/workflows/build.yml, job parser-test).
//
// Regressão que motivou o teste: o fim do array "daily" não pode ser achado por
// strchr(']') porque cada dia tem um "v":[...] com ']' interno — isso zerava nDay.
#include <cstdio>
#include <cstring>
#include "../claude_stick/codex_parse.h"

static int g_fails = 0;
static void check(bool cond, const char* msg) {
    printf("%s %s\n", cond ? "  ok:" : "FAIL:", msg);
    if (!cond) g_fails++;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "fixture.json";
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "fixture nao encontrada: %s\n", path); return 2; }
    static char buf[16384];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f); buf[n] = 0; fclose(f);

    CodexUsage u; memset(&u, 0, sizeof(u));
    parseWindow(buf, "h5", u.has5h, u.pct5, u.after5, u.reset5Epoch);
    parseWindow(buf, "d7", u.has7d, u.pct7, u.after7, u.reset7Epoch);
    u.allowed = boolField(buf, "allowed", false);
    strField(buf, "plan", u.plan, sizeof(u.plan));
    parseAnalytics(buf, u);

    // janelas + campos escalares
    check(u.has7d, "janela 7d presente");
    check(u.pct7 > 0 && u.pct7 <= 100, "7d used_percent no intervalo");
    check(u.plan[0] != 0, "plan parseado");
    check(u.allowed, "allowed = true");

    // analytics
    check(u.hasAn, "hasAn");
    check(u.interactions > 0, "interactions > 0");
    check(u.creditsTotal > 0, "credits_total > 0");
    check(u.nSurface >= 1 && u.nSurface <= CXAN_ROWS, "nSurface no limite");
    check(u.surface[0].key[0] != 0, "surface[0] tem chave");
    check(u.surface[0].pct >= u.surface[u.nSurface - 1].pct, "surface ordenado desc por %");
    check(u.nModel >= 1 && u.nModel <= CXAN_ROWS, "nModel no limite");
    check(u.model[0].key[0] != 0, "model[0] tem chave");
    check(u.nSurfOrder == u.nSurface, "surf_order alinha com by_surface");

    // *** a regressão: daily precisa parsear TODOS os dias (o ']' interno do v[]) ***
    check(u.nDay >= 1, "nDay >= 1 (bug do ']' interno do v[])");

    // v[] por origem soma ~ credits do dia (±1 por origem, arredondamento)
    long vsum_total = 0;
    bool all_days_ok = true;
    for (int i = 0; i < u.nDay; i++) {
        int sum = 0;
        for (int s = 0; s < CXAN_SURF; s++) { sum += u.day[i].v[s]; vsum_total += u.day[i].v[s]; }
        int diff = sum - (int)u.day[i].credits; if (diff < 0) diff = -diff;
        if (diff > CXAN_SURF) all_days_ok = false;   // tolera arredondamento por origem
    }
    check(all_days_ok, "soma(v[]) ~= credits em todos os dias");
    check(vsum_total > 0, "algum v[] por origem foi parseado (nao tudo zero)");

    if (g_fails) { printf("\n%d verificacao(oes) falharam\n", g_fails); return 1; }
    printf("\nTODAS as verificacoes passaram (nDay=%d, interactions=%u)\n",
           u.nDay, (unsigned)u.interactions);
    return 0;
}
