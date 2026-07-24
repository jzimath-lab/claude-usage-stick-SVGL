#!/usr/bin/env bash
#
# Build / upload / monitor do sketch de BRING-UP (validacao de hardware).
#
# Existe separado do claude_stick/build.sh por um motivo concreto: esta pasta
# nao tem partitions.csv, entao NAO pode usar PartitionScheme=custom. Com o FQBN
# do projeto o build falha com:
#   cp: .../tools/partitions/.csv: No such file or directory
# O esquema de particao e irrelevante aqui — o sketch so valida display e touch.
set -euo pipefail

SKETCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STICK_DIR="$(cd "$SKETCH_DIR/../claude_stick" && pwd)"
FQBN="esp32:esp32:esp32s3:PSRAM=opi,FlashSize=16M,PartitionScheme=huge_app,CDCOnBoot=cdc,USBMode=hwcdc,FlashMode=qio"
PORT_DEFAULT="$(ls /dev/cu.usbmodem* 2>/dev/null | head -1)"

# o lv_conf.h vive no sketch principal; o bring-up nao tem copia propria
LVFLAGS="-DLV_CONF_INCLUDE_SIMPLE -I${STICK_DIR}"

cmd="${1:-build}"
port="${2:-$PORT_DEFAULT}"

if [ "$cmd" != "build" ] && [ -z "$port" ]; then
  echo "nenhuma porta /dev/cu.usbmodem* encontrada — a placa esta conectada?" >&2
  exit 1
fi

case "$cmd" in
  monitor) exec arduino-cli monitor -p "$port" -c baudrate=115200 ;;
  build|upload)
    args=(--fqbn "$FQBN"
          --build-property "compiler.cpp.extra_flags=$LVFLAGS"
          --build-property "compiler.c.extra_flags=$LVFLAGS")
    [ "$cmd" = "upload" ] && args+=(--upload -p "$port")
    echo "==> $cmd ($FQBN)"
    arduino-cli compile "${args[@]}" "$SKETCH_DIR"
    ;;
  *) echo "comando desconhecido: $cmd (use: build | upload | monitor)" >&2; exit 1 ;;
esac
