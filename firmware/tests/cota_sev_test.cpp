// Teste de host dos limiares de severidade do §7 aplicados ao stick.
//
//   c++ -std=c++17 -Wall -Wextra -Werror -I../claude_stick -o cota_sev_test cota_sev_test.cpp
#include <cstdio>
#include "../claude_stick/cota_sev.h"

static int f = 0;
static void ok(bool c, const char* m) { printf("%s %s\n", c?"  ok:":"FAIL:", m); if(!c) f++; }

int main() {
    // ⚠️ Bordas, e nao valores do meio: e onde um `>` no lugar de `>=` se
    // esconde, e e exatamente o ponto em que o alerta muda de significado.
    ok(cota_sev(0)     == CSEV_OK,        "0% -> verde");
    ok(cota_sev(74)    == CSEV_OK,        "74% -> verde (ultima do verde)");
    ok(cota_sev(74.9f) == CSEV_OK,        "74,9% ainda verde");
    ok(cota_sev(75)    == CSEV_ATENCAO,   "75% -> laranja (primeira do laranja)");
    ok(cota_sev(90)    == CSEV_ATENCAO,   "90% -> laranja (o caso real de hoje)");
    ok(cota_sev(99.9f) == CSEV_ATENCAO,   "99,9% ainda laranja");
    ok(cota_sev(100)   == CSEV_ESTOURADA, "100% -> vermelho (primeira do vermelho)");
    ok(cota_sev(368)   == CSEV_ESTOURADA, "368% -> vermelho");

    // As cores, para o stick e o painel nao divergirem em silencio.
    ok(cota_sev_cor(74)  == 0x4ADE80, "verde  = C_OK");
    ok(cota_sev_cor(90)  == 0xFBBF24, "laranja = C_WARN");
    ok(cota_sev_cor(100) == 0xF87171, "vermelho = C_BAD");

    // ⚠️ O DEFEITO QUE ISTO CONSERTA: com a fonte trocada para a VPS, o
    // statusOverall passou a ser "allowed" FIXO, e status_color() devolvia
    // C_OK sempre — chip verde a 90%. A cor agora sai do NUMERO, que o payload
    // realmente carrega, e nao de um texto que ele nao tem.
    ok(cota_sev_cor(90) != 0x4ADE80, "90% NAO pode sair verde");

    printf(f ? "\n%d FALHA(S)\n" : "\nTUDO OK\n", f);
    return f ? 1 : 0;
}
