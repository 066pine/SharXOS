#!/bin/bash
# SharkXOS automated regression suite.
#
# Every suite boots the real ISO in QEMU with no display and drives the kernel
# through the serial console, exactly like a human typing at the shell. Logs are
# written to tests/logs/<suite>.log. QEMU is killed by `timeout`, so exit code
# 124 is the normal outcome for suites that do not power the machine off.
#
# usage: tools/run_all_tests.sh [suite ...]

cd "$(dirname "$0")/.." || exit 1

OUT=tests/logs
mkdir -p "$OUT"

if [ ! -f build/sharkxos.iso ]; then
  echo "error: build/sharkxos.iso not found - run 'make' first" >&2
  exit 1
fi

QEMU=${QEMU:-qemu-system-i386}
QFLAGS="-cdrom build/sharkxos.iso -m 128M -display none -serial stdio -monitor none -no-reboot"

declare -A TIMEOUT=(
  [smoke1]=40
  [smoke2]=60
  [vfs2]=35
  [sync3]=55
  [states1]=40
  [calc1]=40
  [editor1]=50
  [monitor1]=40
)
declare -A DELAY=(
  [smoke1]=5
  [smoke2]=5
  [vfs2]=5
  [sync3]=5
  [states1]=5
  [calc1]=5
  [editor1]=5
  [monitor1]=5
)

# Expected evidence per suite. Each entry is "suite:pattern"; a suite passes
# only when every one of its patterns is present in the captured log.
EXPECT=(
  "smoke1:Kernel initialisation complete"
  "smoke1:VGA read/write   : PASS"
  "smoke1:PIT ticking      : PASS"
  "smoke1:memtest: PASS (8/8 checks)"
  "smoke1:thread states: PASS"
  "smoke1:semaphore test: PASS"
  "smoke2:access_test: PASS"
  "smoke2:race test: PASS"
  "smoke2:lock ordering: PASS"
  "smoke2:usermode: PASS"
  "vfs2:vfs exhaustion: PASS"
  "sync3:deadlock demo: PASS"
  "sync3:usermode: PASS"
  "states1:thread states: PASS"
  "calc1:= 14"
  "calc1:= 1.414213562"
  "calc1:error: division by zero"
  "editor1:saved 78 bytes to 'story.txt'"
  "editor1:tale.txt"
  "monitor1:SharkXOS system monitor"
  "monitor1:system monitor closed"
)

SUITES=("$@")
if [ ${#SUITES[@]} -eq 0 ]; then
  SUITES=(smoke1 smoke2 vfs2 sync3 states1 calc1 editor1 monitor1)
fi

overall=0
for suite in "${SUITES[@]}"; do
  cmds="tests/$suite.txt"
  if [ ! -f "$cmds" ]; then
    echo "SKIP $suite (missing $cmds)"
    continue
  fi
  tmo=${TIMEOUT[$suite]:-60}
  delay=${DELAY[$suite]:-5}
  log="$OUT/$suite.log"
  printf 'RUN  %-9s (timeout %ss) ... ' "$suite" "$tmo"

  ( sleep "$delay"; cat "$cmds"; sleep 12 ) | \
    timeout "$tmo" "$QEMU" $QFLAGS > "$log" 2>&1
  code=$?

  ok=1
  for entry in "${EXPECT[@]}"; do
    case "$entry" in
      "$suite":*)
        pattern=${entry#*:}
        if ! grep -Fq "$pattern" "$log"; then
          ok=0
          echo
          echo "     missing: $pattern"
        fi
        ;;
    esac
  done

  if grep -Fq "PANIC" "$log" || grep -Fq "triple fault" "$log"; then
    ok=0
    echo
    echo "     kernel panic detected"
  fi

  if [ $ok -eq 1 ]; then
    echo "PASS ($(grep -c 'sharkx>' "$log") prompts, qemu exit $code)"
  else
    echo "FAIL (see $log)"
    overall=1
  fi
done

echo
if [ $overall -eq 0 ]; then
  echo "ALL REQUESTED SUITES PASSED (logs in $OUT/)"
else
  echo "ONE OR MORE SUITES FAILED (logs in $OUT/)"
fi
exit $overall
