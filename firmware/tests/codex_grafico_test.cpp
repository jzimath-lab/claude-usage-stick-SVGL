// Teste de host da altura das colunas do grafico diario do Codex.
//
//   c++ -std=c++17 -Wall -Wextra -Werror -I../claude_stick -o codex_grafico_test codex_grafico_test.cpp
//
// O QUE MOTIVOU (medido em 30/08 contra o payload real do bridge):
//   - 28 de 38 segmentos (73%) caiam no piso de 2px e ficavam indistinguiveis
//   - 5 das 14 colunas tinham 6px ou menos: dias de 9 e de 22 creditos
//     desenhavam igual
//   - a coluna de 21/08 media 62px com CXDAY_MAXH = 58, ESTOURANDO o teto
//
// As duas ultimas eram o mesmo bug. `if (h < 2) h = 2` num grafico EMPILHADO
// soma 2px por origem minuscula: o piso nao so achatava a informacao pequena,
// ele INFLAVA os totais, e inflava mais os dias com mais origens ativas.
//
// A troca: altura da coluna pela RAIZ QUADRADA do total (uma variacao de 100x
// entre 9 e 903 creditos nao cabe linearmente em 58px), e os segmentos
// repartindo essa altura por maior-resto, de modo que a soma bata EXATAMENTE
// com a coluna — sem piso, sem estouro.
#include <cstdio>
#include <cstdlib>
#include "../claude_stick/codex_grafico.h"

static int f = 0;
static void ok(bool c, const char* m) { printf("%s %s\n", c?"  ok:":"FAIL:", m); if(!c) f++; }

static int soma(const int* a, int n) { int s = 0; for (int i = 0; i < n; i++) s += a[i]; return s; }

#define MAXH 58
static const uint16_t MX = 903;   // maior dia do periodo medido

int main() {
    int h[5];

    // 1. O dia mais alto ocupa exatamente o teto — nunca mais que ele.
    {
        const uint16_t v[5] = {873, 13, 7, 10, 0};        // 21/08 real: estourava 62px
        int col = cxColunaAlturas(v, 5, 903, MX, MAXH, h);
        ok(col == MAXH, "dia maximo -> altura == teto (era 62 com teto 58)");
        ok(soma(h, 5) == col, "os segmentos somam a coluna: sem piso que infla");
    }

    // 2. A propriedade que o bug violava, agora em varios dias reais.
    {
        const uint16_t dias[4][5] = {{873,13,7,10,0},{0,0,1,0,8},{0,4,0,0,18},{21,0,0,0,0}};
        const uint16_t tot[4] = {903, 9, 22, 21};
        bool somaBate = true, dentroDoTeto = true;
        for (int d = 0; d < 4; d++) {
            int col = cxColunaAlturas(dias[d], 5, tot[d], MX, MAXH, h);
            if (soma(h, 5) != col) somaBate = false;
            if (col > MAXH) dentroDoTeto = false;
        }
        ok(somaBate,     "em todo dia real, soma dos segmentos == altura da coluna");
        ok(dentroDoTeto, "em nenhum dia real a coluna passa do teto");
    }

    // 3. Raiz quadrada: um quarto do total da METADE da altura, nao um quarto.
    {
        const uint16_t v[5] = {225, 0, 0, 0, 0};
        int col = cxColunaAlturas(v, 5, 225, 900, 60, h);   // 225/900 = 1/4
        ok(col == 30, "1/4 do maximo -> 1/2 da altura (raiz quadrada)");
    }

    // 4. O ganho concreto: 9 e 22 creditos deixam de desenhar igual.
    {
        const uint16_t a[5] = {0,0,1,0,8}, b[5] = {0,4,0,0,18};
        int ca = cxColunaAlturas(a, 5, 9,  MX, MAXH, h);
        int cb = cxColunaAlturas(b, 5, 22, MX, MAXH, h);
        ok(ca != cb, "dias de 9 e 22 creditos agora se distinguem (ambos davam 2px)");
        ok(ca > 0,   "o menor dia com consumo continua visivel");
    }

    // 5. Dia sem consumo nao desenha nada — ausencia e diferente de pouco.
    {
        const uint16_t v[5] = {0,0,0,0,0};
        int col = cxColunaAlturas(v, 5, 0, MX, MAXH, h);
        ok(col == 0 && soma(h, 5) == 0, "dia zerado -> coluna zero");
    }

    // 6. Origem dominante nao pode roubar a coluna inteira por arredondamento.
    {
        const uint16_t v[5] = {873, 13, 7, 10, 0};
        cxColunaAlturas(v, 5, 903, MX, MAXH, h);
        ok(h[0] > h[1] && h[1] > 0, "a maior origem lidera, mas as menores sobrevivem");
    }

    // 7. maxTotal degenerado nao pode dividir por zero nem estourar.
    {
        const uint16_t v[5] = {0,0,0,0,0};
        int col = cxColunaAlturas(v, 5, 0, 0, MAXH, h);
        ok(col >= 0 && col <= MAXH, "maxTotal=0 nao quebra");
    }

    // 8. MONOTONICIDADE — a propriedade que o desenho antigo violava sem que
    //    ninguem notasse. Nos dados reais de 30/08, um dia de 21 creditos
    //    desenhava 2px e um de 9 creditos desenhava 4px: o piso somava por
    //    SEGMENTO, entao a altura media quantas origens o dia usou, nao quanto
    //    ele consumiu. Um histograma de diversidade disfarcado de volume.
    {
        const uint16_t dias[5][5] = {
            {0,0,1,0,8},        //  9 creditos, 2 origens
            {21,0,0,0,0},       // 21 creditos, 1 origem   <- desenhava MENOR
            {0,4,0,0,18},       // 22 creditos, 2 origens
            {95,0,8,0,0},       // 103 creditos
            {873,13,7,10,0},    // 903 creditos, 4 origens
        };
        const uint16_t tot[5] = {9, 21, 22, 103, 903};
        int ant = -1; bool cresce = true;
        for (int d = 0; d < 5; d++) {
            int col = cxColunaAlturas(dias[d], 5, tot[d], MX, MAXH, h);
            if (col < ant) cresce = false;
            ant = col;
        }
        ok(cresce, "mais creditos NUNCA desenha mais baixo, seja qual for o no. de origens");
    }

    // 8b. O caso que REALMENTE inverte: dia pequeno e DISPERSO contra dia maior
    //     e concentrado. Com piso por segmento, 9 creditos em 5 origens somam
    //     10px e passam por cima de 21 creditos numa origem so, que da 9px.
    //     Sem este caso, o teste 8 passava com e sem o piso — e eu teria
    //     concluido que ele protegia algo que nao protegia.
    {
        const uint16_t disperso[5] = {2,2,2,2,1};      //  9 creditos, 5 origens
        const uint16_t concentr[5] = {21,0,0,0,0};     // 21 creditos, 1 origem
        int cd = cxColunaAlturas(disperso, 5,  9, MX, MAXH, h);
        int cc = cxColunaAlturas(concentr, 5, 21, MX, MAXH, h);
        ok(cd < cc, "dia disperso de 9 nao pode superar dia concentrado de 21");
    }

    printf(f ? "\nFALHOU (%d)\n" : "\nOK\n", f);
    return f ? 1 : 0;
}
