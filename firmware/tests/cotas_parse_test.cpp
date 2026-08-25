// Teste de host do parser das cotas vindas da VPS da estacao.
// Cobre o MESMO cotas_parse.h que roda no device.
//
// Uso:  c++ -std=c++17 -Wall -Wextra -Werror -o cotas_parse_test cotas_parse_test.cpp
//       ./cotas_parse_test cotas_fixture.json
//
// ⚠️ A FIXTURE E O PAYLOAD REAL do `/api/display`, capturado em 25/08/2026 do
// servidor de producao — nao um exemplo escrito a mao a partir do design. A
// forma de um payload nao se adivinha: este projeto ja teve um teste passando
// pela razao errada porque a fixture trazia o campo sob teste em `null` e
// portanto nao exibia a forma.
//
// E ela guarda o documento INTEIRO (clima, grafico, infra, alertas, sol...),
// nao so o pedaco de cotas: o parser precisa achar a sua parte no meio do
// resto, que e o que ele enfrenta no device.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "../claude_stick/cotas_parse.h"

static int g_fails = 0;
static void check(bool cond, const char* msg) {
    printf("%s %s\n", cond ? "  ok:" : "FAIL:", msg);
    if (!cond) g_fails++;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "cotas_fixture.json";
    FILE* f = fopen(path, "rb");
    if (!f) { printf("nao abriu %s\n", path); return 2; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char* buf = (char*)malloc((size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { printf("leitura curta\n"); return 2; }
    buf[n] = 0; fclose(f);

    CotasVps c;
    bool ok = parseCotas(buf, c);
    check(ok, "parseCotas aceita o payload real do /api/display");

    // --- o provedor com dados -------------------------------------------
    const CotaProv* codex = cotaAchar(c, "codex");
    check(codex != nullptr, "acha o provedor codex no meio do documento inteiro");
    if (codex) {
        check(strcmp(codex->plano, "Plus") == 0, "plano do codex vem como TEXTO literal");
        check(!codex->temErro, "codex sem erro");
        check(codex->nJanelas == 2, "codex tem 2 janelas (session e weekly)");
        check(codex->janela[0].usadoPct > 0, "usado_pct do codex e numero > 0");
        // ⚠️ EXATO, nao "plausivel". A versao anterior deste check era
        // `> 1700000000`, e ela ficaria VERDE com a conversao errada — que e a
        // pergunta que separa teste de teatro: ele passaria tambem com o
        // defeito presente? A conversao de calendario e escrita a mao (mktime
        // interpretaria em fuso LOCAL e daria 3 h de erro no device), entao e
        // precisamente onde um valor quase-certo se esconde.
        check(codex->janela[0].resetaEm == 1787685122u,
              "reseta_em 2026-08-25T19:12:02Z -> epoch EXATO 1787685122");
        check(codex->janela[1].resetaEm == 1788271922u,
              "reseta_em 2026-09-01T14:12:02Z -> epoch EXATO 1788271922");
    }

    // --- o provedor em erro ---------------------------------------------
    // ⚠️ Este e o caso que importa. Um provedor recusado NAO pode contaminar os
    // outros: foi exatamente esse o Critical do S1, onde um shape inesperado
    // derrubava o motor de alertas inteiro e a tela exibia saude perfeita.
    const CotaProv* claude = cotaAchar(c, "claude");
    check(claude != nullptr, "acha o provedor claude mesmo em erro");
    if (claude) {
        check(claude->temErro, "claude marcado como em erro");
        check(claude->nJanelas == 0, "claude sem janelas quando o fetch falhou");
        check(strstr(claude->erro, "claude.ai") != nullptr, "o erro chega legivel");
    }

    // --- o isolamento, medido -------------------------------------------
    int integros = 0;
    for (uint8_t i = 0; i < c.nProv; i++) if (!c.prov[i].temErro) integros++;
    check(c.nProv == 6, "os SEIS provedores foram lidos");
    check(integros == 5, "cinco integros: o erro de um nao derrubou os demais");

    // --- frescor ---------------------------------------------------------
    check(c.idadeS >= 0, "idade_s presente");
    check(c.geradoEm[0] != 0, "gerado_em presente");

    // --- texto que NAO pode ser interpretado ------------------------------
    // §3 do design: saldo chega como string de exibicao ("Balance: $8.12") e vai
    // LITERAL para a tela. Extrair o numero dali nos acoplaria a formatacao do
    // CodexBar, que ele e livre para mudar sem quebrar contrato.
    const CotaProv* orouter = cotaAchar(c, "openrouter");
    check(orouter && strstr(orouter->plano, "Balance:") != nullptr,
          "saldo do openrouter preservado como TEXTO, sem parsing");

    // --- bissexto, que a fixture nao exerce ------------------------------
    // 29/02/2024 so existe se o ajuste de ano bissexto estiver certo. Sem este
    // caso, o parser erraria por um dia em todo mes >= marco de ano bissexto e
    // nada na fixture denunciaria.
    // ⚠️ 29/02 NAO exerce o ajuste de bissexto: fevereiro tem M=2, e a correcao
    // so vale para M>2. O primeiro teste que escrevi aqui usava 29/02, tinha
    // "bissexto" no nome e cobertura ZERO — a mutacao `dias += 0` passou
    // incolume por ele. Quem denunciou foi o teste de mutacao, nao a leitura.
    // Marco de ano bissexto e o menor caso que realmente cruza a linha.
    check(cotaEpoch("2024-02-29T12:00:00Z") == 1709208000u, "29/02/2024 (fim de fevereiro)");
    check(cotaEpoch("2024-03-01T00:00:00Z") == 1709251200u,
          "01/03/2024 -> epoch EXATO — ESTE exerce o ajuste de bissexto");
    check(cotaEpoch("2023-03-01T00:00:00Z") == 1677628800u,
          "01/03/2023 (ano comum) -> o ajuste NAO pode ser aplicado");
    check(cotaEpoch("2026-12-31T23:59:59Z") == 1798761599u, "virada de ano");
    check(cotaEpoch("1970-01-01T00:00:00Z") == 0u, "epoch zero");
    check(cotaEpoch("lixo") == 0u, "string invalida vira 0, sem estourar");

    free(buf);
    printf(g_fails ? "\n%d FALHA(S)\n" : "\nTUDO OK\n", g_fails);
    return g_fails ? 1 : 0;
}
