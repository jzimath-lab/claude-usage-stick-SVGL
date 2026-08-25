#pragma once
// Credenciais da VPS da estacao — funcoes PURAS de string, sem deps Arduino,
// para serem testaveis no host. Incluido pelo firmware e por
// firmware/tests/vps_creds_test.cpp.
//
// POR QUE UM SEGUNDO CONJUNTO DE CREDENCIAIS
// A entrada de token que ja existe (web_server.cpp, handleTokenPost) valida o
// valor chamando `fetchUsage()` — ou seja, perguntando a ANTHROPIC se ele
// presta. Isso e correto para o token dela e inutil para qualquer outro
// segredo: a credencial da VPS precisa ser validada contra a VPS.
//
// ⚠️ E NAO sao cifradas com o PIN, ao contrario do token da Anthropic. O PIN
// protege uma credencial que AGE na conta (POST /v1/messages); estas so LEEM um
// snapshot de cota. Cifra-las exigiria digitar o PIN para o aparelho voltar a
// mostrar percentuais depois de uma queda de energia — que e exatamente o
// incidente que originou este trabalho.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define VPS_HOST_MAX   64
#define VPS_USER_MAX   32
#define VPS_PASS_MAX   64
#define VPS_TOKEN_MAX  80

struct VpsCreds {
    char host[VPS_HOST_MAX];    // so o hostname: "estacao-display.exemplo"
    char user[VPS_USER_MAX];    // basicAuth da borda (opcional)
    char pass[VPS_PASS_MAX];
    char token[VPS_TOKEN_MAX];  // X-Device-Token (obrigatorio)
};

static const char VPS_B64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/*
 * Monta "Basic <base64(user:pass)>".
 *
 * ⚠️ Devolve false se nao couber, em vez de truncar. Header truncado gera um
 * 401 que ninguem associa ao tamanho do buffer — o operador procuraria a senha
 * errada por horas.
 */
static bool vpsAuthBasic(const char* user, const char* pass, char* out, size_t sz) {
    if (!user || !pass || !out) return false;
    const size_t lu = strlen(user), lp = strlen(pass);
    const size_t bruto = lu + 1 + lp;                 // "user:pass"
    const size_t b64 = ((bruto + 2) / 3) * 4;
    if (sizeof("Basic ") - 1 + b64 + 1 > sz) return false;

    memcpy(out, "Basic ", 6);
    char* o = out + 6;

    // O separador e o PRIMEIRO ':': dois-pontos dentro da senha sao legais em
    // basicAuth e precisam sobreviver inteiros.
    uint8_t tri[3];
    size_t n = 0;
    for (size_t k = 0; k < bruto; k++) {
        char c = (k < lu) ? user[k] : (k == lu ? ':' : pass[k - lu - 1]);
        tri[n++] = (uint8_t)c;
        if (n == 3) {
            *o++ = VPS_B64[tri[0] >> 2];
            *o++ = VPS_B64[((tri[0] & 0x03) << 4) | (tri[1] >> 4)];
            *o++ = VPS_B64[((tri[1] & 0x0F) << 2) | (tri[2] >> 6)];
            *o++ = VPS_B64[tri[2] & 0x3F];
            n = 0;
        }
    }
    if (n == 1) {
        *o++ = VPS_B64[tri[0] >> 2];
        *o++ = VPS_B64[(tri[0] & 0x03) << 4];
        *o++ = '='; *o++ = '=';
    } else if (n == 2) {
        *o++ = VPS_B64[tri[0] >> 2];
        *o++ = VPS_B64[((tri[0] & 0x03) << 4) | (tri[1] >> 4)];
        *o++ = VPS_B64[(tri[1] & 0x0F) << 2];
        *o++ = '=';
    }
    *o = 0;
    return true;
}

/*
 * Monta a URL do /api/display a partir do HOSTNAME puro.
 *
 * ⚠️ Recusa host que ja traga esquema ou barra. Colar "https://..." no campo e
 * o erro de digitacao mais provavel, e concatenar produziria
 * "https://https://..." — um erro de rede sem relacao aparente com a causa.
 */
static bool vpsUrlDisplay(const char* host, char* out, size_t sz) {
    if (!host || !out || host[0] == 0) return false;
    if (strchr(host, '/') || strchr(host, ':') || strchr(host, ' ')) return false;
    int n = snprintf(out, sz, "https://%s/api/display", host);
    return n > 0 && (size_t)n < sz;
}

// O device token e obrigatorio; o basicAuth da borda e opcional, porque nem
// todo vhost o exige.
static bool vpsCredsValidas(const VpsCreds& c) {
    return c.host[0] != 0 && c.token[0] != 0;
}
