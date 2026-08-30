// Teste de host da combinacao das duas fontes do Codex.
//
//   c++ -std=c++17 -Wall -Wextra -Werror -I../claude_stick -o codex_merge_test codex_merge_test.cpp
//
// O QUE MOTIVOU: apos a consolidacao do S4 o `fetch_codex` fazia `g_codex =
// bridge` e sobrescrevia so as JANELAS com as da estacao. A flag `stale`
// continuava sendo a do bridge, entao um 401 no bridge fazia a tela escrever
// "DADO VELHO" sobre percentuais que vinham FRESCOS da estacao.
//
// A licao: ao combinar fontes, os METADADOS tambem precisam ser combinados.
// Cada metade do payload tem a sua propria frescura.
#include <cstdio>
#include <cstring>
#include "../claude_stick/codex_merge.h"

static int f = 0;
static void ok(bool c, const char* m) { printf("%s %s\n", c?"  ok:":"FAIL:", m); if(!c) f++; }

static CodexUsage zerado() { CodexUsage u; memset(&u, 0, sizeof(u)); return u; }

// bridge tipico: janelas proprias, analytics presentes
static CodexUsage bridgeCom(bool datado) {
    CodexUsage b = zerado();
    b.ok = true; b.stale = datado;
    b.has5h = true; b.pct5 = 11; b.has7d = true; b.pct7 = 22;
    b.hasAn = true; b.interactions = 777; b.creditsTotal = 3973;
    return b;
}

// estacao: SO janelas, nunca analytics (o /api/display nao as carrega)
static CodexUsage estacaoCom() {
    CodexUsage e = zerado();
    e.ok = true; e.stale = false;
    e.has5h = true; e.pct5 = 33; e.has7d = true; e.pct7 = 44;
    return e;
}

int main() {
    const CodexUsage nada = zerado();

    // 1. O caso real de 30/08: bridge com http_401, estacao fresca.
    {
        CodexUsage b = bridgeCom(true), e = estacaoCom(), out = zerado();
        ok(codexCombinar(&b, &e, nada, out), "bridge datado + estacao: combina");
        ok(out.pct5 == 33 && out.pct7 == 44, "janelas vem da ESTACAO, nao do bridge");
        ok(out.stale == false,   "cotas NAO sao datadas — vieram frescas da estacao");
        ok(out.anStale == true,  "analytics SAO datadas — vieram do bridge em 401");
        ok(out.interactions == 777, "analytics preservam o valor do bridge");
    }

    // 2. Tudo fresco: nenhuma marcacao.
    {
        CodexUsage b = bridgeCom(false), e = estacaoCom(), out = zerado();
        codexCombinar(&b, &e, nada, out);
        ok(out.stale == false && out.anStale == false, "tudo fresco: nada marcado");
    }

    // 3. Sem estacao: as janelas passam a ser do bridge, entao herdam a idade dele.
    {
        CodexUsage b = bridgeCom(true), out = zerado();
        codexCombinar(&b, nullptr, nada, out);
        ok(out.pct5 == 11, "sem estacao, janelas vem do bridge");
        ok(out.stale == true && out.anStale == true, "so bridge datado: AS DUAS metades datadas");
    }

    // 4. Bridge caiu: a estacao ainda da janelas frescas, e as analytics do
    //    ciclo anterior continuam na tela — mas datadas por definicao.
    {
        CodexUsage e = estacaoCom(), out = zerado();
        CodexUsage anterior = bridgeCom(false);
        ok(codexCombinar(nullptr, &e, anterior, out), "bridge fora + estacao: combina");
        ok(out.pct5 == 33, "janelas frescas da estacao");
        ok(out.stale == false, "cotas seguem frescas mesmo com o bridge fora");
        ok(out.interactions == 777, "analytics do ciclo anterior sao PRESERVADAS");
        ok(out.anStale == true, "analytics preservadas sao datadas por definicao");
    }

    // 5. Nenhuma fonte: nao ha o que exibir.
    {
        CodexUsage out = zerado();
        ok(!codexCombinar(nullptr, nullptr, nada, out), "as duas fontes fora: recusa");
    }

    printf(f ? "\nFALHOU (%d)\n" : "\nOK\n", f);
    return f ? 1 : 0;
}
