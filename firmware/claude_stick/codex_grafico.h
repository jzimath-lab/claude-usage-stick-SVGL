#pragma once
#include <stdint.h>
#include <math.h>

/*
 * Alturas empilhadas de UMA coluna do grafico diario do Codex.
 *
 * O QUE ISTO SUBSTITUI (medido em 30/08 contra o payload real do bridge)
 * ---------------------------------------------------------------------
 * O desenho anterior escalava cada segmento linearmente pelo maior dia e
 * aplicava `if (h < 2) h = 2` para o pequeno nao sumir. Num grafico EMPILHADO
 * isso soma 2px por origem minuscula:
 *
 *   - 28 de 38 segmentos (73%) ficavam no piso, indistinguiveis entre si
 *   - a coluna de 21/08 media 62px com teto de 58 — ESTOURAVA
 *
 * O piso nao so achatava a informacao pequena: inflava os totais, e inflava
 * MAIS os dias com mais origens ativas. O grafico mentia na direcao oposta a
 * intuicao — dia disperso parecia maior que dia concentrado de igual consumo.
 *
 * COMO FUNCIONA AGORA
 * -------------------
 * 1. A altura da coluna sai da RAIZ QUADRADA da fracao do maior dia. O periodo
 *    medido varia 100x (9 a 903 creditos); linearmente, cinco dos catorze dias
 *    caem para 2px e desenham igual. Com raiz, 9 e 22 creditos viram 6 e 9px.
 * 2. Os segmentos repartem essa altura por MAIOR-RESTO. A soma bate exatamente
 *    com a coluna, entao nao ha piso a inflar nem estouro a corrigir.
 *
 * Segmento que arredonda para zero desaparece — de proposito. A composicao fina
 * e trabalho da tela de Origem, que ja mostra os percentuais por extenso; aqui
 * o que importa e o formato do periodo.
 *
 * Devolve a altura da coluna, que e sempre igual a soma de out[0..n-1].
 */
/*
 * Altura de UMA coluna, por RAIZ QUARTA da fracao do maior dia.
 *
 * POR QUE RAIZ QUARTA, E NAO QUADRADA
 * -----------------------------------
 * A raiz quadrada entrou primeiro e estava matematicamente certa: tirou o menor
 * dia de 1px para 6px. Na tela continuou lendo como ZERO ao lado de 58px — o
 * usuario fotografou o card "7 DIAS" com os dias 27 a 30 como riscos.
 *
 * O consumo diario varia ~100x (9 a 903 creditos). Sob raiz quadrada isso
 * ainda e 10x; sob raiz quarta vira ~3x, que cabe numa barra de 66px.
 *
 * O log foi descartado: comprime demais o topo — 103 e 903 creditos, que sao
 * 9x diferentes, virariam 40 e 58px. Passaria a mentir na outra direcao.
 *
 * Um dia COM consumo nunca desenha 0: o piso de 1px aqui e da COLUNA inteira,
 * nao por segmento — foi o piso por segmento que inflava e invertia os totais.
 */
static inline int cxAlturaColuna(uint32_t total, uint32_t maxTotal, int maxH) {
    if (total == 0 || maxTotal == 0 || maxH <= 0) return 0;
    float frac = (float)total / (float)maxTotal;
    if (frac > 1.0f) frac = 1.0f;
    int h = (int)(sqrtf(sqrtf(frac)) * maxH + 0.5f);
    if (h > maxH) h = maxH;
    if (h < 1)    h = 1;
    return h;
}

static inline int cxColunaAlturas(const uint16_t* v, int n, uint16_t total,
                                  uint16_t maxTotal, int maxH, int* out) {
    for (int i = 0; i < n; i++) out[i] = 0;
    if (total == 0 || maxTotal == 0 || maxH <= 0) return 0;

    int col = cxAlturaColuna(total, maxTotal, maxH);   // uma escala so nos dois graficos
    if (col < 1) col = 1;                             // ha consumo: a coluna existe

    const int LIM = 16;
    int m = n < LIM ? n : LIM;
    float resto[LIM];
    int usado = 0;
    for (int i = 0; i < m; i++) {
        float exato = (float)v[i] / (float)total * col;
        out[i]  = (int)exato;
        resto[i] = exato - out[i];
        usado   += out[i];
    }

    // Os pixels perdidos no arredondamento vao para os maiores restos. E isto
    // que faz a soma fechar com a coluna sem recorrer a um piso.
    while (usado < col) {
        int melhor = -1;
        for (int i = 0; i < m; i++)
            if (v[i] > 0 && (melhor < 0 || resto[i] > resto[melhor])) melhor = i;
        if (melhor < 0) break;    // entrada incoerente (total > 0, v[] todo zero)
        out[melhor]++;
        resto[melhor] = -1.0f;
        usado++;
    }
    return usado;
}
