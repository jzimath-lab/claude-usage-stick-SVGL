#pragma once
// Severidade de uma cota a partir do PERCENTUAL — §7 do S4, aprovado em 25/08.
//
// ⚠️ POR QUE ISTO SUBSTITUI O status_color()
// Aquele decide pelo status TEXTUAL da API (`allowed_warning`, `rejected`), que
// vinha dos cabecalhos `unified-*` da Anthropic. Desde que a fonte passou a ser
// a VPS, esse campo nao existe no payload e o mapeamento o preenche com
// "allowed" FIXO — entao status_color() devolvia C_OK sempre, e o chip do
// Claude ficava VERDE a 90%. Nao quebrava, nao logava: so parava de avisar.
//
// A cor agora sai do NUMERO, que o payload realmente carrega.
//
// Limiares: ate 74% verde · 75-99% laranja · 100%+ vermelho.
#include <stdint.h>

typedef enum { CSEV_OK, CSEV_ATENCAO, CSEV_ESTOURADA } cota_sev_t;

static inline cota_sev_t cota_sev(float pct) {
    // ⚠️ `>=` nas bordas: 75 e a PRIMEIRA do laranja e 100 a PRIMEIRA do
    // vermelho. Trocar por `>` desloca cada faixa em um ponto — defeito que so
    // morde no limiar, que e onde o alerta muda de significado.
    if (pct >= 100.0f) return CSEV_ESTOURADA;
    if (pct >= 75.0f)  return CSEV_ATENCAO;
    return CSEV_OK;
}

// As mesmas C_OK/C_WARN/C_BAD do config.h, repetidas aqui como literais para o
// header compilar no HOST sem arrastar o Arduino. Os valores sao verificados
// pelo teste — se o config.h mudar e este nao, o teste denuncia.
static inline uint32_t cota_sev_cor(float pct) {
    switch (cota_sev(pct)) {
    case CSEV_ESTOURADA: return 0xF87171;   /* C_BAD  */
    case CSEV_ATENCAO:   return 0xFBBF24;   /* C_WARN */
    default:             return 0x4ADE80;   /* C_OK   */
    }
}
