# SharkXOS – Test Plan

## 1. Strategy

SharkXOS is tested by **booting the real ISO in QEMU and driving it through the
serial console**. There is no mocking and no host-side simulation: every test
executes the same kernel that a human would use. The console mirrors VGA output
to COM1, and bytes received on COM1 are injected into the PS/2 keyboard queue,
so command scripts typed into the serial port reach the shell exactly like real
keystrokes.

Tests are grouped into **suites**. Each suite is a text file of shell commands
(`tests/<suite>.txt`). `tools/run_all_tests.sh` runs a suite with

```bash
( sleep <delay>; cat tests/<suite>.txt; sleep 12 ) | \
  timeout <seconds> qemu-system-i386 -cdrom build/sharkxos.iso -m 128M \
      -display none -serial stdio -monitor none -no-reboot > tests/logs/<suite>.log
```

and then asserts that specific strings appear in the log. A suite **passes**
only if:

1. every expected string for that suite is present;
2. the log contains no `PANIC` and no `triple fault`;
3. the boot line `Kernel initialisation complete` is present (for boot suites).

Self-checking shell commands (`memtest`, `vfstest`, `race`, `deadlock`,
`lockorder`, `access_test`, `usermode`, `states`) compute their own verdict and
print `PASS`/`FAIL`. The harness checks those verdicts rather than trusting a
fixed transcript.

## 2. Environment

| Item | Value |
|---|---|
| Host | Ubuntu 24.04.5 LTS on WSL2, kernel 6.18.54.1 |
| Compiler | `i686-elf-g++` (GCC) 15.2.0, binutils 2.45 |
| Assembler | NASM 2.16.01 |
| Bootloader | GRUB 2.12 (`grub-mkrescue`) |
| Emulator | QEMU 8.2.2 (`qemu-system-i386`) |
| Machine | 128 MiB RAM, BIOS, `-no-reboot` |
| Display | none; serial console on stdio |

## 3. Suites

| Suite | Focus | Timeout |
|---|---|---|
| `smoke1` | boot banner, help, version, about, uptime, echo, diag, threads, scheduler, states, semtest, memory, heapinfo, memtest | 40 s |
| `smoke2` | VFS CRUD, permissions, `su`, access_test, race, lockorder, deadlock, ring 3, ring 3 fault | 60 s |
| `vfs2` | RAM-disk exhaustion (`vfstest`) and recovery | 35 s |
| `sync3` | deadlock, ring 3 normal + fault (repeat run in a fresh boot) | 55 s |
| `states1` | five thread states, twice, plus `threads` snapshots | 40 s |
| `calc1` | calculator arithmetic, functions, modes, bases, memory, errors, history | 40 s |
| `editor1` | editor create/edit/save/reload/quit, heap bracketing, file manager CRUD | 50 s |
| `monitor1` | system monitor, then shell/VFS after it exits | 40 s |

## 4. Test cases

### 4.1 Boot and diagnostics (`smoke1`)

| ID | Input | Expected |
|---|---|---|
| BOOT-1 | boot | `Kernel initialisation complete. 4 threads running.` |
| BOOT-2 | `help` | command table, 37 registered commands |
| BOOT-3 | `version` | `SharkXOS version 1.0.0`, compiler string |
| BOOT-4 | `about` | project description |
| BOOT-5 | `uptime` | increasing milliseconds and tick count at 100 Hz |
| BOOT-6 | `echo hello sharkx` | `hello sharkx` |
| BOOT-7 | `diag` | `VGA read/write : PASS`, `PIT ticking : PASS`, paging enabled, 4 threads, 8192 KiB heap |
| BOOT-8 | `threads` | idle/shell/heartbeat/memwatch, shell RUNNING |
| BOOT-9 | `scheduler` | preemptive round robin, non-zero ticks/switches/preemptions |
| BOOT-10 | `memory`, `heapinfo` | real heap and RAM-disk numbers |
| BOOT-11 | `memtest` | `memtest: PASS (8/8 checks)` |

### 4.2 Thread states (`states1`)

| ID | Input | Expected |
|---|---|---|
| THR-1 | `states` | `observed   : NEW READY RUNNING WAITING TERMINATED` and `thread states: PASS` |
| THR-2 | `threads` | the demo thread is `TERMINATED` |
| THR-3 | `states` again | a second thread (new id) passes the same checks |

### 4.3 Synchronisation (`smoke2`, `sync3`)

| ID | Input | Expected |
|---|---|---|
| SYN-1 | `race 100` | unsynchronised run loses updates; mutex run equals 200; `race test: PASS` |
| SYN-2 | `lockorder` | counter 200, no deadlock, `lock ordering: PASS` |
| SYN-3 | `deadlock` | circular wait reported, cycle detected, both threads terminated, `deadlock demo: PASS` |
| SYN-4 | `scheduler` after SYN-2/3 | no mutex owned by a terminated thread |
| SEM-1 | `semtest` | two consumers block at count 0; one signal releases exactly one; second signal releases the other; final count 0; `semaphore test: PASS` |

### 4.4 Heap (`smoke1`)

| ID | Input | Expected |
|---|---|---|
| HEAP-1 | `memtest` | alignment, writable payload, double-free rejection, invalid-free rejection, coalescing, OOM, realloc, stress: 8/8 |
| HEAP-2 | `heapinfo` | counters consistent; no unexpected double/invalid frees |

### 4.5 VFS (`smoke2`, `vfs2`, `editor1`)

| ID | Input | Expected |
|---|---|---|
| VFS-1 | `ls` on empty disk | `no files` message |
| VFS-2 | `touch notes.txt` | file created, real owner and permissions printed |
| VFS-3 | `write notes.txt hello sharkx vfs` | `appended 17 bytes` |
| VFS-4 | `cat notes.txt` | the same text |
| VFS-5 | `ls`, `diskinfo` | size 17, 1 file, capacity/usage consistent |
| VFS-6 | `rm notes.txt`, `ls` | file gone |
| VFS-7 | `vfstest` | four 64 KiB writes accepted, fifth `ENOSPC`, missing file reported, cleanup 0 bytes, `PASS` |
| VFS-8 | editor flow | save 78 bytes, reload, rename, delete via file manager |

### 4.6 Access control (`smoke2`)

| ID | Input | Expected |
|---|---|---|
| SEC-1 | `whoami` | `principal: SYSTEM` |
| SEC-2 | `su user`, `whoami` | `now acting as USER`, principal USER |
| SEC-3 | `su restricted`, `cat`/`write` | read allowed, write/delete denied |
| SEC-4 | `permissions notes.txt` | owner, USER and RESTRICTED masks |
| SEC-5 | `access_test` | 7/7 checks, `access_test: PASS` |
| SEC-6 | `usermode` | text printed from CPL=3, live SYS_TICKS, exit code 0, `PASS` |
| SEC-7 | `usermode fault` | `cli` raises #GP (vector 13), kernel continues, shell responsive |

### 4.7 Applications

| ID | Input | Expected |
|---|---|---|
| CALC-1 | `2+3*4` | `= 14` |
| CALC-2 | `sin(pi/2)`, `5!`, `sqrt(2)`, `2^10` | `= 1`, `= 120`, `= 1.414213562`, `= 1024` |
| CALC-3 | `deg`, `sin(90)`, `tan(45)` | `= 1`, `= 1` |
| CALC-4 | `bin 255`, `hex 255` | `0b11111111`, `0xff` |
| CALC-5 | `ncr(5,2)`, `npr(5,2)` | `= 10`, `= 20` |
| CALC-6 | `m+ 42`, `mr` | memory 42 |
| CALC-7 | `1/0`, `sqrt(-1)` | division-by-zero and sqrt-domain errors |
| CALC-8 | `hist`, `exit` | history lines, clean return to shell |
| EDIT-1 | create `story.txt` with 2 lines, `:l` | numbered lines |
| EDIT-2 | `:e 2`, `:i 1`, `:l` | updated numbered buffer |
| EDIT-3 | `:w`, `:q` | `saved 78 bytes`, returns to shell |
| EDIT-4 | `cat story.txt` | saved text |
| EDIT-5 | reopen, `:q!` | loads file, quits without warning |
| EDIT-6 | `heapinfo` before and after editor | peak rises by ~14 KiB for the two heap buffers, then a 14 368-byte free block reappears; no leak attributable to the editor |
| FILE-1 | `files` + `ls/cat/info/ren/rm/exit` | real VFS operations |
| MON-1 | `monitor`, any key | live system screen; returns to shell |
| MON-2 | `version/files/ls` after monitor | shell still functional |

### 4.8 Build system

| ID | Command | Expected |
|---|---|---|
| BLD-1 | `make clean` | `build/` removed |
| BLD-2 | `make` | ELF32 + Multiboot2 checks pass, ISO created |
| BLD-3 | `make iso` | ISO rebuilt/no-op |
| BLD-4 | `make run` | boots to `sharkx>` headless |
| BLD-5 | `make debug` | QEMU paused, GDB stub message, port 1234 |
| BLD-6 | `make test` | smoke suite runs, log written |

## 5. Pass/fail rules

* A suite passes only when all expected strings for it are found.
* Any `PANIC` or `triple fault` is an automatic failure.
* A shell command that reports its own `FAIL` is a failure even if the suite's
  other strings are present.
* A QEMU exit code of `124` is expected and not a failure.
* Warnings from the compiler are recorded but do not fail the build; they are
  listed in `KNOWN_ISSUES.md`.

## 6. Out of scope

The following are intentionally not covered by automated tests and are
documented as limitations instead:

* SMP behaviour (the kernel is UP-only);
* persistence across reboot (the RAM disk is volatile);
* adversarial security (ring 3 has no memory isolation);
* long-duration soak testing (suites run under a minute each);
* fuzzing of the expression parser or VFS names;
* real hardware (all runs are under QEMU).

## 7. Reproducing

```bash
make clean
make
tools/run_all_tests.sh
```

Logs land in `tests/logs/<suite>.log`. The captured results from the validation
run are in [TEST_RESULTS.md](TEST_RESULTS.md).
