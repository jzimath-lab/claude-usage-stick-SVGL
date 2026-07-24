# token_bridge como serviço (macOS / launchd)

O `token_bridge.py` precisa rodar continuamente para manter a linha de tokens da
tela **Agora** atualizada. Em vez de deixar um `--loop` num terminal aberto, use
um LaunchAgent.

## Instalar

```bash
cp tools/launchd/com.julianoz.claude-usage-stick.plist ~/Library/LaunchAgents/
launchctl load -w ~/Library/LaunchAgents/com.julianoz.claude-usage-stick.plist
```

Ajuste os dois caminhos absolutos no plist (`token_bridge.py` e o log) para a
sua máquina antes de carregar.

## Verificar

```bash
launchctl list | grep claude-usage-stick   # coluna 2 = exit code da última execução
tail -f ~/claude-usage-stick/token-bridge.log
```

## Decisões

- **`StartInterval` (120s), não `--loop` + `KeepAlive`.** Cada execução é
  independente: se o gadget estiver fora do ar, o script sai com erro e a próxima
  tentativa acontece normal. Com `--loop`, um travamento no meio do laço mataria
  o serviço até reboot.
- **`/usr/bin/python3` (o do sistema), não o do PATH.** O script é stdlib puro.
  O launchd roda com ambiente mínimo — depender de pyenv/homebrew via PATH é a
  receita de "funciona no terminal, falha como serviço".

## Gotcha importante

O servidor web do gadget só sobe **com o dashboard aberto** (após o PIN). Depois
de qualquer queda de energia o device para na tela de PIN e o bridge falha em
silêncio (o `/window` não responde) até alguém destravar. Linha de tokens vazia
pode significar "gadget esperando PIN", não "Mac desligado".

## Remover

```bash
launchctl unload ~/Library/LaunchAgents/com.julianoz.claude-usage-stick.plist
rm ~/Library/LaunchAgents/com.julianoz.claude-usage-stick.plist
```
