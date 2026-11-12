#!/bin/bash
# Longer-running targeted test: $1 = log, $2 = command file, $3 = timeout seconds, $4 = input delay
cd "$(dirname "$0")/.."
LOG=${1:-build/run.log}
CMDS=${2:-}
TMO=${3:-120}
DELAY=${4:-5}
( sleep "$DELAY"; cat "$CMDS"; sleep 20 ) | \
  timeout "$TMO" qemu-system-i386 -cdrom build/sharkxos.iso -m 128M \
    -display none -serial stdio -monitor none -no-reboot > "$LOG" 2>&1
echo "=== exit $? ==="
