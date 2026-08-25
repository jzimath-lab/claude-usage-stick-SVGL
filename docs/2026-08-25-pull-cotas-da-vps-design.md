# Claude por pull da VPS — design

**Data:** 25/08/2026 · **Estado:** para aprovação
**Repositórios afetados:** este (firmware) e `estacao/` (servidor na VPS)

---

## 1. O problema, medido

O aparelho busca a cota do Claude fazendo `POST https://api.anthropic.com/v1/messages`
com um token OAuth de assinatura, e lê os cabeçalhos `unified-*` da resposta.
**Desde 25/08 essa chamada devolve HTTP 401** e a tela exibe
`auth_failed · nova tentativa em 240s`.

Quatro causas foram eliminadas na placa, não por raciocínio:

| eliminada | evidência |
|---|---|
| rede / DNS / TLS | Codex e GitHub funcionam **no mesmo boot** |
| PIN / decifragem do token | `[PIN] ok, token 108 chars` |
| header `anthropic-beta` desatualizado | `oauth-2025-04-20` é o valor corrente |
| versão do cliente personificada | trocada para `claude-code/2.1.231` e regravada; **binário conferido** (contém a nova, não contém a antiga) e o 401 persistiu |

Sobra o token — expirado, revogado ou bloqueado por política. As três são
indistinguíveis de fora e **todas se resolvem no mesmo lugar**: gerar outra
credencial. O README deste repositório documenta por que essa saída é
indesejada (a política da Anthropic de 04/04/2026 sobre tokens de assinatura em
ferramenta de terceiro, com a exposição recaindo sobre a conta do usuário).

**Este design existe para tornar a tela independente dessa decisão.**

---

## 2. A ideia

O Mac já produz os percentuais do Claude **sem** token OAuth de terceiro: o
CodexBar os obtém da sessão do claude.ai e da varredura de log local. Medido em
13/08: sessão 4%, semanal 59%, com `resetAt` reais.

Esses números já vão ser empurrados para a VPS da estação (S4, tarefa T1). Este
design faz o stick **puxar de lá**.

```
Mac (codexbar → projeção)  ──push──▶  VPS  ──pull──▶  stick
                                       │
                                       └──pull──▶  painel P4
```

A VPS não é intermediário gratuito: ela é **buffer**. Com o Mac dormindo, o
último snapshot continua servido, com `gerado_em` para o aparelho envelhecer o
dado honestamente em vez de apagá-lo.

### 2.1 O que isto resolve além do 401

- **Tira a credencial perigosa do aparelho.** O token atual pode *enviar
  mensagens* na conta. A credencial da VPS só *lê um snapshot de cota*. Não é
  troca lateral — é redução de raio de dano.
- **Desacopla do PIN.** Hoje o `token_bridge.py` empurra *para* o aparelho, o
  que exige o servidor web dele de pé — ou seja, PIN digitado. Quem puxa não
  precisa disso.

---

## 3. Decisões, e por quê

Estas eram perguntas em aberto; ficam decididas aqui.

| decisão | escolha | razão |
|---|---|---|
| endpoint | **reusar `/api/display`** | a T3 do S4 ainda não foi implantada; isto entra na **mesma janela de deploy** em vez de abrir uma segunda. Rota dedicada é otimização prematura com dois aparelhos |
| credencial | **`X-Device-Token` próprio para o stick** | uma linha de config, entra no mesmo deploy, e comprometer um aparelho não expõe o outro |
| precedência | **pull vence enquanto fresco (< 15 min)** | sem heurística. Passando disso, envelhece em cinza; a chamada própria só entra se o pull falhar |
| TLS | **nada a fazer** | o bundle do firmware já traz a **ISRG Root X1**, e a VPS serve Let's Encrypt (`issuer CN=YR1`, conferido em 25/08) |
| campos | **7, não 15** | a UI só consome `h5`, `d7`, `h5ResetEpoch`, `d7ResetEpoch`, `statusOverall`, `ok`, `error` — os outros 8 do `UsageData` não aparecem em lugar nenhum |

---

## 4. Firmware

### 4.1 Um módulo novo, `usage_pull.cpp`

Preenche o **mesmo `UsageData`** que o `api.cpp` preenche hoje. A UI não muda:
ela continua lendo `g_usage.h5`, `g_usage.d7` e os resets, sem saber de onde vieram.

```
bool pullUsage(UsageData& out);   // GET https://<vhost>/api/display
```

`api.cpp` **não é tocado.** Continua como reserva, exatamente como decidido —
hoje inerte, porque o token está recusado.

### 4.2 Onde entra no ciclo

O ciclo já tem passos nomeados (`ntp`, `claude/usage`, `codex`, `github`). O
passo `claude/usage` passa a:

```
1. pullUsage()            → sucesso: usa, e marca a origem
2. fetchUsage()           → só se o pull falhar
3. os dois falharam       → mantém o último valor, envelhecendo
```

⚠️ **O passo 3 é requisito, não acabamento.** O princípio que governa o painel
da estação — *dado morto continua na tela, apagado* — vale igual aqui: apagar a
coluna esconde a informação de que ela existia.

### 4.3 Credenciais no aparelho

`basicAuth` da borda e `X-Device-Token` vão para a NVS, ao lado do WiFi.

⚠️ **Não são cifrados com o PIN, e isso é deliberado.** O PIN protege uma
credencial que age na conta; esta só lê cota. Cifrá-la significaria exigir o PIN
para o aparelho mostrar percentuais depois de uma queda de energia — que é
precisamente o incidente que originou este trabalho.

---

## 5. Servidor (VPS) — exige janela do Juliano

Mudança mínima, e nada aqui é novo: é a mesma forma do `deviceToken` que já existe.

1. aceitar um **segundo** `X-Device-Token` válido (o do stick)
2. `.htpasswd` do vhost ganha o par do stick, ou reusa o existente

**Não** cria rota, não muda payload, não mexe no `cotas.js`.

---

## 6. Testes

| o quê | onde | como |
|---|---|---|
| parser do payload → `UsageData` | host, sem placa | fixture do `/api/display` real, como o S2 fez com o parser de clima |
| precedência pull > API | host | pull fresco vence; pull velho perde; os dois mortos preservam o anterior |
| segundo device token | `estacao/tests/api.test.js` | token do stick 200; token inválido 401 |

⚠️ Para cada teste, a pergunta da casa: **ele ficaria verde também com o defeito
presente?** A fixture precisa ter valores que *discordem* entre si — o S2 já
teve um teste de precedência que passava por qualquer caminho porque as duas
fontes davam o mesmo número.

---

## 7. O que este design NÃO faz

- **Não remove o token do aparelho.** Decisão do Juliano: a API fica como reserva.
- **Não gera credencial nova da Anthropic.** É justamente o que ele evita.
- **Não toca Codex nem GitHub.** Têm fontes próprias, funcionando.
- **Não porta o portão de glifos da estação.** Lá ele varre todos os literais de
  `main/**`; aqui daria **sete falsos positivos**, porque este firmware serve
  HTML ao navegador (onde "página" com acento está certa) e escreve no serial.
  O critério certo é "literal que o LVGL desenha", e distingui-lo exige ler o
  destino de cada string.

---

## 8. O risco que fica

**O `sessionKey` do claude.ai expira**, e é dele que o CodexBar tira os
percentuais. Quando expirar, o Mac para de produzir, a VPS serve o último
snapshot, e o aparelho o exibe envelhecendo — degradação honesta, mas
degradação. Renovar é colar o cookie de novo.

Isto **não** é regressão em relação a hoje: hoje a coluna está em `auth_failed`
permanente. É trocar uma falha definitiva por uma renovável.
