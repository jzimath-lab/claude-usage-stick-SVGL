// Teste de host das credenciais da VPS — cobre o MESMO vps_creds.h que roda no
// device. Sem Arduino: sao funcoes puras de string.
//
// Uso:  c++ -std=c++17 -Wall -Wextra -Werror -o vps_creds_test vps_creds_test.cpp
//       ./vps_creds_test
#include <cstdio>
#include <cstring>
#include "../claude_stick/vps_creds.h"

static int g_fails = 0;
static void check(bool cond, const char* msg) {
    printf("%s %s\n", cond ? "  ok:" : "FAIL:", msg);
    if (!cond) g_fails++;
}

int main() {
    char buf[128];

    // --- Authorization: Basic -------------------------------------------
    // ⚠️ Valores EXATOS, conferidos contra base64 de referencia. "comeca com
    // Basic " ficaria verde com qualquer codificacao errada.
    check(vpsAuthBasic("estacao", "senha123", buf, sizeof(buf)), "monta o Basic");
    check(strcmp(buf, "Basic ZXN0YWNhbzpzZW5oYTEyMw==") == 0,
          "estacao:senha123 -> Basic ZXN0YWNhbzpzZW5oYTEyMw== (EXATO)");

    // Sem padding: 3 bytes viram 4 chars sem '='.
    check(vpsAuthBasic("a", "b", buf, sizeof(buf)) && strcmp(buf, "Basic YTpi") == 0,
          "a:b -> Basic YTpi (multiplo de 3, sem padding)");

    // ⚠️ Dois-pontos na SENHA e legal em basicAuth: so o PRIMEIRO separa. Se a
    // implementacao cortasse no ultimo, a senha sairia truncada e o 401
    // pareceria credencial errada do usuario.
    check(vpsAuthBasic("user", "p:with:colons", buf, sizeof(buf))
          && strcmp(buf, "Basic dXNlcjpwOndpdGg6Y29sb25z") == 0,
          "dois-pontos na senha sobrevivem");

    // Um padding: "estacao:" tem 8 bytes.
    check(vpsAuthBasic("estacao", "", buf, sizeof(buf))
          && strcmp(buf, "Basic ZXN0YWNhbzo=") == 0, "senha vazia ainda codifica");

    // --- limites de buffer ----------------------------------------------
    // Truncar silenciosamente produziria um header quase-certo, e um 401 que
    // ninguem associa ao tamanho do buffer.
    char curto[10];
    check(!vpsAuthBasic("estacao", "senha123", curto, sizeof(curto)),
          "buffer curto RECUSA em vez de truncar");

    // --- URL --------------------------------------------------------------
    check(vpsUrlDisplay("estacao-display.srv1390429.hstgr.cloud", buf, sizeof(buf))
          && strcmp(buf, "https://estacao-display.srv1390429.hstgr.cloud/api/display") == 0,
          "monta a URL do /api/display");

    // Host colado com esquema e um erro de digitacao provavel; aceitar geraria
    // "https://https://..." e um erro de rede sem relacao aparente com a causa.
    check(!vpsUrlDisplay("https://estacao.exemplo", buf, sizeof(buf)),
          "host com esquema e RECUSADO, nao concatenado");
    check(!vpsUrlDisplay("", buf, sizeof(buf)), "host vazio recusado");
    check(!vpsUrlDisplay("com/barra", buf, sizeof(buf)), "host com barra recusado");

    // --- credenciais completas -------------------------------------------
    VpsCreds c = {};
    check(!vpsCredsValidas(c), "credenciais zeradas sao invalidas");
    strcpy(c.host, "estacao.exemplo"); strcpy(c.token, "abc123");
    check(vpsCredsValidas(c), "host + token bastam (basicAuth e opcional)");
    strcpy(c.token, "");
    check(!vpsCredsValidas(c), "sem device token, invalidas");

    printf(g_fails ? "\n%d FALHA(S)\n" : "\nTUDO OK\n", g_fails);
    return g_fails ? 1 : 0;
}
