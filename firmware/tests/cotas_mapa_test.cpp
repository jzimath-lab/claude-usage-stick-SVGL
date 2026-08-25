// Teste de host do mapeamento CotasVps -> UsageData: a peca que faz o payload
// da VPS alimentar exatamente os mesmos campos que a busca direta alimentava,
// para que a UI nao saiba de onde o dado veio.
//
// Uso:  c++ -std=c++17 -Wall -Wextra -Werror -o cotas_mapa_test cotas_mapa_test.cpp
//       ./cotas_mapa_test cotas_fixture.json
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "../claude_stick/cotas_mapa.h"

static int g_fails = 0;
static void check(bool c, const char* m) { printf("%s %s\n", c?"  ok:":"FAIL:", m); if(!c) g_fails++; }

static char* ler(const char* p) {
    FILE* f = fopen(p, "rb"); if (!f) return nullptr;
    fseek(f,0,SEEK_END); long n=ftell(f); fseek(f,0,SEEK_SET);
    char* b=(char*)malloc((size_t)n+1);
    if (fread(b,1,(size_t)n,f)!=(size_t)n) { fclose(f); return nullptr; }
    b[n]=0; fclose(f); return b;
}

int main(int argc, char** argv) {
    char* body = ler(argc>1?argv[1]:"cotas_fixture.json");
    if (!body) { printf("fixture ausente\n"); return 2; }
    CotasVps c; parseCotas(body, c);

    // --- o caso da fixture: claude EM ERRO --------------------------------
    UsageData u = {};
    bool ok = cotasParaUsage(c, u);
    check(!ok, "claude em erro -> mapeamento devolve false");
    check(!u.ok, "UsageData.ok = false");
    check(strstr(u.error, "claude.ai") != nullptr, "a mensagem do servidor chega ao campo error");

    // ⚠️ O QUE NAO PODE ACONTECER: erro nao pode deixar percentuais sujos. Se
    // h5/d7 viessem preenchidos com lixo, a UI mostraria numero inventado no
    // lugar onde deveria envelhecer o ultimo valor bom.
    check(u.h5 == 0.0f && u.d7 == 0.0f, "erro NAO deixa percentual residual");

    // --- provedor com janelas: constroi um caso a partir do codex ---------
    // O codex tem session+weekly na fixture; renomeio para "claude" e verifico
    // que os dois tipos caem nos campos certos. Sem isto, trocar session por
    // weekly no mapeamento passaria despercebido — os dois sao float.
    CotasVps c2 = c;
    for (uint8_t i = 0; i < c2.nProv; i++) {
        if (strcmp(c2.prov[i].id, "claude") == 0) strcpy(c2.prov[i].id, "claude_off");
        if (strcmp(c2.prov[i].id, "codex")  == 0) strcpy(c2.prov[i].id, "claude");
    }
    UsageData v = {};
    check(cotasParaUsage(c2, v), "provedor saudavel -> true");
    check(v.ok, "UsageData.ok = true");

    const CotaProv* p = cotaAchar(c2, "claude");
    float esperadoH5 = 0, esperadoD7 = 0; uint32_t rH5 = 0, rD7 = 0;
    for (uint8_t i = 0; i < p->nJanelas; i++) {
        if (!strcmp(p->janela[i].tipo, "session")) { esperadoH5 = p->janela[i].usadoPct; rH5 = p->janela[i].resetaEm; }
        if (!strcmp(p->janela[i].tipo, "weekly"))  { esperadoD7 = p->janela[i].usadoPct; rD7 = p->janela[i].resetaEm; }
    }
    check(v.h5 == esperadoH5, "session -> h5 (e nao d7)");
    check(v.d7 == esperadoD7, "weekly  -> d7 (e nao h5)");
    check(v.h5ResetEpoch == rH5 && v.d7ResetEpoch == rD7, "os resets acompanham as janelas certas");
    check(esperadoH5 != esperadoD7, "os dois valores DISCORDAM na fixture — senao o teste nao separaria");

    // --- provedor ausente --------------------------------------------------
    CotasVps c3 = {};
    UsageData w = {};
    check(!cotasParaUsage(c3, w), "sem provedor claude -> false");
    check(w.error[0] != 0, "e diz por que, em vez de falhar mudo");

    free(body);
    printf(g_fails ? "\n%d FALHA(S)\n" : "\nTUDO OK\n", g_fails);
    return g_fails ? 1 : 0;
}
