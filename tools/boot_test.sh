#!/bin/bash
# SharkXOS automated boot/interaction test over the serial console.
# usage: boot_test.sh <logfile> [seconds_before_input] [command_file]
set -u
cd "$(dirname "$0")/.."

LOG=${1:-build/boot.log}
DELAY=${2:-4}
CMDS=${3:-}

if [ -n "$CMDS" ]; then
  ( sleep "$DELAY"; cat "$CMDS"; sleep 6 ) | \
    timeout 45 qemu-system-i386 -cdrom build/sharkxos.iso -m 128M \
      -display none -serial stdio -monitor none -no-reboot > "$LOG" 2>&1
else
  timeout 12 qemu-system-i386 -cdrom build/sharkxos.iso -m 128M \
    -display none -serial stdio -monitor none -no-reboot > "$LOG" 2>&1
fi

echo "=== exit code $? ; log: $LOG ($(wc -l < "$LOG") lines) ==="
