# Runbook — Claude Usage Stick

Guia operacional para reflashar, diagnosticar e recuperar o gadget **daqui a 6
meses, sem lembrar de nada**. Fatos concretos desta placa, não teoria.

---

## A placa (confirmada por eFuse em 23/07/2026)

| | |
|---|---|
| Chip | ESP32-S3 (QFN56) rev v0.2 |
| PSRAM | 8 MB **octal** (S3R8), `PSRAM=opi` obrigatório |
| Flash | 16 MB quad |
| Display | AXS15231B, QSPI 40 MHz, 480×320 |
| Touch | AXS15231B, I²C `0x3B` |
| Porta USB no Mac | `/dev/cu.usbmodem*` — **renumera** (já foi `2101` e `1101`); o `build.sh` autodetecta |
| MAC / hostname | `28:84:85:49:51:bc` · `claude-stick.local` |

## Toolchain (versões que funcionam — não atualizar sem motivo)

```
arduino-cli 1.5.1  ·  core esp32:esp32 3.3.11  ·  GFX 1.6.5  ·  lvgl 9.2.2
```

O core e as libs geram o binário; o `arduino-cli` só orquestra.

O core subiu de 3.3.8 para **3.3.11** em 13/08/2026, acompanhando o upstream, e a
troca foi **medida** antes de ser adotada — mesmo código, só trocando o
compilador:

| | 3.3.8 | 3.3.11 |
|---|---:|---:|
| binário | 2.029.270 B (12%) | **1.998.306 B (11%)** |
| variáveis globais | 79.948 B | 80.248 B |

Compila limpo nos dois; o 3.3.11 gera ~31 KB a menos de flash e 300 B a mais de
RAM estática. Para reverter: `arduino-cli core install esp32:esp32@3.3.8`.

**A lvgl 9.5.0 continua deixada de lado de propósito** — só o core mudou.

`lv_conf.h` precisa estar visível ao LVGL. O `build.sh` passa
`-DLV_CONF_INCLUDE_SIMPLE -I<sketch>`; se mesmo assim der `lv_conf.h not found`,
copie `firmware/claude_stick/lv_conf.h` para `~/Documents/Arduino/libraries/`.

---

## Reflashar (o caminho normal)

```bash
cd firmware/claude_stick
./build.sh              # só compila
./build.sh upload       # compila + grava (porta autodetectada)
./build.sh monitor      # serial a 115200
```

**A gravação preserva WiFi e token.** O flash escreve tabela de partições,
otadata e app — não toca a NVS, onde ficam a rede e o token cifrado. Depois de
gravar, o device pede **só o PIN**. Não refaz onboarding.

O sketch de bring-up (valida só display+touch) tem o seu próprio:

```bash
cd firmware/bringup
./build.sh upload
```

---

## Onboarding do zero (após "Apagar tudo" ou troca de placa)

1. **WiFi** — toque a rede, digite a senha no teclado da tela (até 3 redes na NVS).
2. **Token** — a tela mostra o IP do device. Gere o token no Mac:
   ```bash
   claude setup-token          # abre OAuth no navegador → sk-ant-oat01-...
   ```
   Abra o IP no navegador (mesma rede) e cole o token. O device valida na hora.
3. **PIN** — 4 dígitos, digitados duas vezes. O token é cifrado com ele
   (AES-256-GCM, chave derivada do PIN). **O PIN nunca é armazenado.**

> 10 PINs errados **apagam** as credenciais e voltam ao onboarding (lockout
> dobra a cada erro).

---

## Token bridge (contagem real de tokens na tela)

A API não expõe contagem para conta de assinatura. O `token_bridge.py` lê os
transcripts locais do Claude Code e empurra os números para o device.

Roda como serviço launchd — ver [`tools/launchd/README.md`](tools/launchd/README.md).

```bash
launchctl list | grep claude-usage-stick    # coluna 2 = exit code
tail -f ~/claude-usage-stick/token-bridge.log
/usr/bin/python3 tools/token_bridge.py      # rodar um envio à mão
```

---

## Diagnóstico

| Sintoma | Causa provável | O que fazer |
|---|---|---|
| Tela mostra `%` 1 p.p. diferente do painel | **Atraso de poll** (device atualiza a cada 120s; painel, sob demanda) | Tocar a barra de refresh do device e atualizar o painel na sequência antes de comparar. Não é bug — o painel arredonda, o device também |
| Linha de tokens vazia | Bridge nunca configurado **ou** device na tela de PIN | Se o device está no PIN, o servidor web não sobe → bridge falha em silêncio. Destravar |
| Linha de tokens "há Xm" em cinza | Dado velho (>15 min) | Bridge parou. Ver o log e o `launchctl list` |
| `curl claude-stick.local/window` recusa conexão | Dashboard não está aberto | O servidor web só existe em `ui_main()` (após o PIN). Destravar o device |
| Tela "Falha" com "nova tentativa em Ns" | API/rede fora | **Normal agora** — o device tenta sozinho com backoff. Não trava mais |
| Cores invertidas (vermelho ↔ azul) | `LV_COLOR_16_SWAP` | Virar para `1` no `lv_conf.h` e regravar. **Nesta placa o valor certo é `0`** |
| Porta não encontrada no upload | Cabo/enumeração | `ls /dev/cu.usbmodem*`; reconectar. A porta renumera entre conectores |

### Ler o serial sem o arduino-cli

`scratchpad/readserial.py` pulsa DTR/RTS para resetar o S3 e captura o boot:

```bash
python3 readserial.py 12     # 12 segundos de log
```

Boot saudável:
```
=== Claude Usage Stick (touch) ===
[WDT] armado em 90000 ms
WiFi: connected to '...'! IP=10.0.0.x
```

---

## Recuperação

### Restaurar o firmware de fábrica

Backup feito antes da primeira gravação:

```bash
esptool --port /dev/cu.usbmodemXXXX write-flash 0x0 \
  ~/claude-usage-stick/backups/factory-16MB-20260723.bin
```
(sha256 `10b0dd2478855a445553057e9cb6b5a1954b82034273b6fb92429e36940add1a`)

### Device travado / reboot infinito

O watchdog (90s) e o `ESP.restart()` nos erros fatais já cobrem travamento em
software. Se persistir, é hardware ou flash corrompido: regravar; se não subir,
restaurar o backup de fábrica.

### Identificar a placa por USB (sem gravar nada)

```bash
esptool --port /dev/cu.usbmodemXXXX flash-id      # chip, PSRAM, flash
espefuse --port /dev/cu.usbmodemXXXX summary       # PSRAM_CAP, FLASH_TYPE
```
(`esptool` num venv em `scratchpad/esptool-venv/`)

---

## Gotchas que custaram tempo (não repetir)

- **`PartitionScheme=custom` exige `partitions.csv` na pasta do sketch.** O
  `bringup/` não tem — por isso usa `huge_app`. Só o `claude_stick/` usa `custom`.
- **`static` em header é bomba de linkagem.** Definir membro/variável em escopo
  de arquivo num `.h` incluído por 2+ `.cpp` dá `multiple definition` — erro que
  aparece no fim do build e aponta para o header, não para quem incluiu.
- **O bridge depende do dashboard aberto.** Token cifrado atrás do PIN → servidor
  web só sobe após destravar. Reboot do gadget = bridge parado até digitar o PIN.
