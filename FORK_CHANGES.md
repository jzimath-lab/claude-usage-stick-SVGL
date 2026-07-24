# Mudanças deste fork

Fork de [benevid/claude-usage-stick-SVGL](https://github.com/benevid/claude-usage-stick-SVGL)
(por sua vez fork de [oauramos/claude-usage-stick](https://github.com/oauramos/claude-usage-stick)),
a partir do commit `5f078ce`.

> **Licença:** o upstream não tem `LICENSE` — por padrão, todos os direitos
> reservados. [Issue aberta pedindo licenciamento](https://github.com/oauramos/claude-usage-stick/issues/16).
> Enquanto não houver resposta, este fork **não pode ser publicado** como projeto
> próprio; o trabalho aqui é local.

## Correções de build (enviadas como [PR upstream #1](https://github.com/benevid/claude-usage-stick-SVGL/pull/1))

O repositório **não compilava em máquina limpa**. Duas correções:

- **`lv_conf.h`** — `#include <stdint.h>` sem guarda. O `lv_conf_internal.h`
  inclui o `lv_conf.h` durante a montagem dos `.S` do LVGL, e o assembler
  engasgava nos `typedef`. Nenhum dos dois workarounds do README contornava.
  Fix: `#ifndef __ASSEMBLY__`.
- **`touch.h`** — `AXS15231B_Touch::_instance` definido em escopo de arquivo no
  header. Quebrava a linkagem assim que um segundo `.cpp` incluísse. Fix: `inline`.

## Qualidade de engenharia

- **Monolito quebrado:** `claude_stick.ino` de **2278 → 284 linhas**, em 16
  módulos (`display`, `storage`, `ui_*`, `web_server`, `history`, ...). Nenhum
  arquivo acima de 500 linhas.
- **CI** (`.github/workflows/build.yml`): compila os dois sketches com core/libs
  pinados + gate que **falha** o build se algum arquivo passar de 500 linhas.
- **`.gitignore`** cobre artefatos Python; um `.pyc` versionado foi removido.

## Resiliência

- **`ST_ERROR` deixou de ser beco sem saída.** A tela de "Falha" não tinha botão
  e o loop só atualizava em `ST_MAIN` — uma falha no primeiro fetch travava o
  device até reboot manual. Agora tenta sozinho.
- **Backoff exponencial** (x2/x4/x8, teto 15 min) em falhas consecutivas.
- **`ESP.restart()`** nos erros fatais de display/PSRAM (antes: `while(1)`).
- **Task watchdog** de 90s.
- **`save_history()` atômico** — escrevia com `"w"` (trunca antes de gravar);
  disco cheio destruía o histórico salvo. Agora `.tmp` + rename.

## UX e tooling

- Dado de token velho fica **visível** (mesmos valores, esmaecidos, com a idade)
  em vez de sumir da tela.
- `User-Agent` / `anthropic-beta` saíram de hardcoded para `config.h`, com nota
  explicando que são o disfarce que faz a API aceitar o token do Claude Code.
- Log de usage com 2 casas decimais (era `%.0f`, impossível diagnosticar
  arredondamento).
- `build.sh` autodetecta a porta USB.
- `bringup/` ganhou `build.sh` próprio; `touch.h` duplicado foi eliminado.
- Token bridge documentado como serviço launchd (`tools/launchd/`).

## Documentação

- [`RUNBOOK.md`](RUNBOOK.md) — reflash, onboarding, diagnóstico, recuperação.
- Este arquivo.

> O `README.md` herdado do upstream ainda descreve a estrutura antiga (arquivo
> único). Ver `RUNBOOK.md` e `FORK_CHANGES.md` para o estado atual.
