// Busca as cotas do Claude na VPS da estacao, e decide a precedencia entre esta
// fonte e a busca direta na Anthropic.
//
// POR QUE EXISTE: a busca direta responde HTTP 401 desde 25/08/2026, e as tres
// causas possiveis desembocam em gerar credencial nova — o que o aviso de
// politica do README desaconselha. Este caminho torna a tela independente
// daquela decisao.
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "app_state.h"
#include "certs.h"
#include "config.h"
#include "cotas_mapa.h"
#include "usage_pull.h"
#include "codex_parse.h"
#include "github_parse.h"

// Corpo do ultimo pull bem-sucedido. UMA requisicao alimenta os TRES blocos:
// cotas.claude, cotas.codex e github. Antes, o ciclo fazia tres requisicoes
// HTTPS a tres vhosts diferentes, com tres credenciais — e quando uma caiu
// (Claude, HTTP 401), o diagnostico teve de percorrer um caminho que so ela
// usava. Ganho colateral: os tres blocos passam a ter UM instante de
// referencia, em vez de tres frescores independentes que a tela nao
// distinguia.
static String g_ultimoCorpo;


bool pullUsage(UsageData& out) {
    // ⚠️ ZERA ANTES DE TENTAR. Sem isto, um pull que falha deixa o corpo do
    // ciclo ANTERIOR em pe, e pullCodex/pullGithub o serviriam como se fosse
    // fresco — dado velho com cara de novo, que e pior que dado ausente e o
    // modo de falha que este projeto mais paga.
    g_ultimoCorpo = String();

    if (!vpsCredsValidas(g_vps)) {
        memset(&out, 0, sizeof(out));
        strlcpy_(out.error, "vps nao configurada", sizeof(out.error));
        return false;
    }
    char url[160];
    if (!vpsUrlDisplay(g_vps.host, url, sizeof(url))) {
        memset(&out, 0, sizeof(out));
        strlcpy_(out.error, "host da vps invalido", sizeof(out.error));
        return false;
    }

    WiFiClientSecure client;
    client.setCACert(CA_BUNDLE);
    HTTPClient https;
    if (!https.begin(client, url)) {
        memset(&out, 0, sizeof(out));
        strlcpy_(out.error, "vps: conexao falhou", sizeof(out.error));
        return false;
    }
    https.addHeader("X-Device-Token", g_vps.token);
    if (g_vps.user[0]) {
        char basic[192];
        if (vpsAuthBasic(g_vps.user, g_vps.pass, basic, sizeof(basic)))
            https.addHeader("Authorization", basic);
    }
    https.setTimeout(API_TIMEOUT_MS);
    int code = https.GET();
    String corpo = (code == 200) ? https.getString() : String();
    https.end();

    if (code != 200) {
        memset(&out, 0, sizeof(out));
        snprintf(out.error, sizeof(out.error), "vps HTTP %d", code);
        Serial.printf("[PULL] %s -> HTTP %d\n", url, code);
        return false;
    }

    g_ultimoCorpo = corpo;

    CotasVps c;
    if (!parseCotas(corpo.c_str(), c)) {
        memset(&out, 0, sizeof(out));
        strlcpy_(out.error, "vps: payload sem cotas", sizeof(out.error));
        return false;
    }

    // ⚠️ FRESCOR. `idade_s` e a idade do snapshot NO SERVIDOR; ela nao inclui o
    // tempo desde que ESTE aparelho buscou. Aqui basta, porque estamos no
    // instante da busca — mas quem exibir a idade depois precisa somar o tempo
    // local, ou uma queda de rede congela a idade junto com o dado e a tela diz
    // "fresco" indefinidamente. Mesma licao do §7.3 do painel da estacao.
    if (c.idadeS >= 0 && c.idadeS > PULL_IDADE_MAX_S) {
        memset(&out, 0, sizeof(out));
        snprintf(out.error, sizeof(out.error), "vps: snapshot com %ds", (int)c.idadeS);
        Serial.printf("[PULL] snapshot velho: %ds\n", (int)c.idadeS);
        return false;
    }

    bool ok = cotasParaUsage(c, out);
    Serial.printf("[PULL] ok=%d 5h=%.0f%% 7d=%.0f%% idade=%ds%s%s\n",
                  (int)ok, out.h5, out.d7, (int)c.idadeS,
                  ok ? "" : " erro=", ok ? "" : out.error);
    return ok;
}

/*
 * Precedencia do §4.2 do design, num lugar SO.
 *
 * ⚠️ do_refresh() e bg_refresh() chamavam fetchUsage() cada um por sua conta.
 * Duplicar a decisao entre eles seria bug garantido: um passaria a preferir a
 * VPS e o outro nao, e o sintoma (percentual que muda ao tocar "atualizar")
 * nao apontaria a causa.
 */
bool obterUsage(UsageData& out) {
    if (pullUsage(out)) return true;

    // A VPS falhou. Tenta a fonte direta — hoje inerte (401), mas mantida por
    // decisao explicita para o dia em que houver credencial valida de novo.
    UsageData direta = {};
    if (fetchUsage(g_token, direta)) { out = direta; return true; }

    // ⚠️ As duas falharam: NAO zera o que estava na tela. O §7 e claro — dado
    // morto continua exibido, apagado, com a hora da ultima leitura. `out` ja
    // carrega o erro do pull, que e o mais informativo dos dois.
    out.ok = false;
    return false;
}

/*
 * Extrai o bloco `github` do MESMO corpo que o pull ja baixou.
 *
 * ⚠️ Nao ha parser novo: as duas formas sao IDENTICAS (13 chaves em comum,
 * medido), porque o vhost `github-usage` que o stick consultava serve o mesmo
 * ghPainel.montar() que a estacao. O aparelho mantinha uma credencial
 * compilada no binario para buscar um dado que a estacao ja servia.
 */
bool pullGithub(GithubUsage& out) {
    if (g_ultimoCorpo.length() == 0) return false;
    const char* g = strstr(g_ultimoCorpo.c_str(), "\"github\"");
    if (!g) return false;
    return ghParse(g, out);
}

// Idem para o Codex, que ja vem no bloco de cotas via CodexBar.
bool pullCodex(CodexUsage& out) {
    if (g_ultimoCorpo.length() == 0) return false;
    CotasVps c;
    if (!parseCotas(g_ultimoCorpo.c_str(), c)) return false;
    const CotaProv* p = cotaAchar(c, "codex");
    if (!p || p->temErro) return false;

    memset(&out, 0, sizeof(out));
    for (uint8_t i = 0; i < p->nJanelas; i++) {
        const CotaJanela& j = p->janela[i];
        // Comparacao por NOME e nao por ordem: trocar session por weekly nao
        // quebra tipo — os dois sao float — e so mostraria o numero errado.
        if (!strcmp(j.tipo, "session")) { out.pct5 = j.usadoPct; out.has5h = true; out.reset5Epoch = j.resetaEm; }
        else if (!strcmp(j.tipo, "weekly")) { out.pct7 = j.usadoPct; out.has7d = true; out.reset7Epoch = j.resetaEm; }
    }
    strlcpy_(out.plan, p->plano, sizeof(out.plan));
    out.ok = true;
    return out.has5h || out.has7d;
}
