# SharkXOS – Test Results

This document records the **actual** validation run of the delivered kernel. All
logs referenced here were produced by booting `build/sharkxos.iso` in QEMU and
capturing the serial console; they are retained in `tests/logs/`.

---

## 1. Build under test

| Item | Value |
|---|---|
| Date | 12 November 2026, 16:08–16:15 (local) |
| Toolchain | `i686-elf-g++` (GCC) 15.2.0, binutils 2.45 |
| Assembler | NASM 2.16.01 |
| Build command | `make clean && make` |
| Kernel | `build/kernel.elf`, 400 076 bytes |
| ISO | `build/sharkxos.iso`, 5 523 456 bytes |
| SHA-256 (kernel) | `4f36f942f17866ff0a9888f1eae661220933ba7884b3f3319528cb8ae4aea444` |
| SHA-256 (ISO) | `3b64685c88be3e770d6538f8dcc99a449545caf2201cec34dfb22aad7e467313` |

Format verification:

```
build/kernel.elf: ELF 32-bit LSB executable, Intel 80386, version 1 (SYSV),
                  statically linked, with debug_info, not stripped
  Class:   ELF32
  Type:    EXEC (Executable file)
  Machine: Intel 80386
multiboot2: OK
```

Build targets exercised:

| Target | Result |
|---|---|
| `make clean` | `build/` removed |
| `make` | kernel + ELF32/Multiboot2 checks + ISO, no errors |
| `make iso` | `make: Nothing to be done for 'iso'` (up to date) |
| `make run` | booted headless to `sharkx>` (killed by timeout after 10 s) |
| `make debug` | QEMU paused with GDB stub on `:1234` (killed after 8 s) |
| `make test` | smoke suite ran, `build/boot.log` written, checks passed |

Compiler warnings remain (unused helpers in `heap.cpp` / `string.cpp`); they are
recorded in `KNOWN_ISSUES.md` (KI-21). No build errors occurred.

## 2. Test run summary

Command:

```bash
tools/run_all_tests.sh
```

Harness output:

```
RUN  smoke1    (timeout 40s) ... PASS (14 prompts, qemu exit 124)
RUN  smoke2    (timeout 60s) ... PASS (24 prompts, qemu exit 124)
RUN  vfs2      (timeout 35s) ... PASS (5 prompts, qemu exit 124)
RUN  sync3     (timeout 55s) ... PASS (7 prompts, qemu exit 124)
RUN  states1   (timeout 40s) ... PASS (5 prompts, qemu exit 124)
RUN  calc1     (timeout 40s) ... PASS (2 prompts, qemu exit 124)
RUN  editor1   (timeout 50s) ... PASS (7 prompts, qemu exit 124)
RUN  monitor1  (timeout 40s) ... PASS (4 prompts, qemu exit 124)

ALL REQUESTED SUITES PASSED (logs in tests/logs/)
```

* 8/8 suites passed.
* 21/21 harness assertions matched (`EXPECT` table in
  `tools/run_all_tests.sh`).
* 40+ additional checks were computed inside the kernel (heap self-test,
  access matrix, race, deadlock, lock ordering, semaphore, VFS exhaustion,
  thread states, ring 3).
* QEMU exit code `124` for every suite is expected: the harness kills QEMU with
  `timeout` after the command script has run.
* No `PANIC` or `triple fault` appears in any log.

## 3. Evidence per suite

### 3.1 `smoke1` – boot, shell, diagnostics, threads, heap

Key captured lines:

```
Kernel initialisation complete. 4 threads running.
SharkXOS shell commands (37 registered):
SharkXOS version 1.0.0 (x86 32-bit (IA-32))
kernel build Nov 12 2026 16:08:44, gcc 15.2.0
uptime: 4330 ms (4.330 s), 433 ticks at 100 Hz
  VGA read/write   : PASS
  PIT ticking      : PASS (2 ticks in ~20 ms)
  Paging           : enabled
  Threads          : 4
  Heap             : 8192 KiB total
threads (4 live, 4 created, running id=2):
  1   idle              READY       220       44
  2   shell             RUNNING     223       44  <- running
  3   heartbeat         READY       0         4
  4   memwatch          WAITING     0         2
scheduler: preemptive round robin
  quantum         : 5 ticks (50 ms)
  timer ticks     : 444
  context switches: 94
  preemptions     : 88
[PASS] allocation + 16 byte alignment
[PASS] writable payload
[PASS] double free rejected
[PASS] invalid free rejected
[PASS] free list coalescing
[PASS] out of memory handled
[PASS] realloc preserves data
[PASS] stress allocation loop
memtest: PASS (8/8 checks)
```

Verdict: **PASS**. The boot banner, all core commands, real scheduler counters
and the full heap self-test are present.

### 3.2 `states1` – five thread states

Run twice in the same boot, each with a fresh thread:

```
thread state demo: id=5 name='state-demo'
  NEW        : created with start_ready=false, not queued
  READY      : thread::start() moved it onto the ready queue
  RUNNING    : entry observed itself RUNNING on the CPU
  WAITING    : blocked in mutex::lock() on a mutex the shell owns
  TERMINATED : returned from its entry and called thread::exit()
  observed   : NEW READY RUNNING WAITING TERMINATED
thread states: PASS
```

The follow-up `threads` listing shows the demo thread as `TERMINATED`, and the
second run uses a new id (6) with the same result. Verdict: **PASS** (2/2 runs).

### 3.3 `smoke1` – semaphore

```
sharkx> semtest
semaphore test: initial count 0, two consumers waiting
  both consumers WAITING : yes
  after 1 signal         : 1 consumer(s) finished
  total finished         : 2 of 2
  final count            : 0
semaphore test: PASS
```

This verifies blocking (`WAITING`), one-permit-at-a-time release and the final
count. Verdict: **PASS**.

### 3.4 `smoke2` – VFS, permissions, sync, ring 3

VFS flow:

```
sharkx> touch notes.txt
created 'notes.txt' (owner SYSTEM, user rwd, restricted r--)
sharkx> write notes.txt hello sharkx vfs
appended 17 bytes to 'notes.txt'
sharkx> cat notes.txt
hello sharkx vfs
sharkx> ls
  notes.txt                  17  SYSTEM     rwd  r--  4.230 s
sharkx> diskinfo
ramdisk: 256 KiB capacity, 0 KiB used, 255 KiB free
```

Access control:

```
principal: SYSTEM
now acting as USER
principal: USER
now acting as RESTRICTED
  [PASS] SYSTEM read        -> allowed
  [PASS] SYSTEM write       -> allowed
  [PASS] USER read          -> allowed
  [PASS] USER write         -> allowed
  [PASS] RESTRICTED read    -> allowed
  [PASS] RESTRICTED write   -> permission denied
  [PASS] RESTRICTED delete  -> permission denied
access_test: PASS (all 7 checks behaved as expected)
```

Race demonstration:

```
race condition test: 2 threads x 100 increments = 200 expected
  without mutex   : counter = 100 (lost 100 updates)
  with mutex      : counter = 200 (lost 0 updates)
race test: PASS - unsynchronised update lost data, mutex fixed it
```

Deadlock and prevention:

```
deadlock demo: A locks m1 then m2, B locks m2 then m1
  circular wait   : thread A waits for m2, thread B waits for m1
  detection       : wait-for cycle found, acquisition aborted
  recovery        : A obtained m2 after B released it -> system continued
  system state    : shell still responsive, both threads terminated
deadlock demo: PASS
lock ordering: PASS (no deadlock)
```

Ring 3:

```
ring 3 demo: executing a user mode payload (CS=0x1b, CPL=3)
[ring3] hello from CPL=3 - this text was printed through int 0x80
[ring3] SYS_TICKS returned a live value from the kernel
[ring3] user task exited with code 0
usermode: PASS (privilege transition ring0 -> ring3 -> ring0 via int 0x80)
```

Verdict: **PASS**.

### 3.5 `vfs2` – RAM-disk exhaustion

```
vfs exhaustion test: 256 KiB ramdisk, 64 KiB per write
  fill0.tmp: 64 KiB accepted (used 64 KiB)
  fill1.tmp: 64 KiB accepted (used 128 KiB)
  fill2.tmp: 64 KiB accepted (used 192 KiB)
  fill3.tmp: 64 KiB accepted (used 256 KiB)
  fill4.tmp: rejected with ENOSPC (no space left)
  accepted writes : 4 x 64 KiB
  ENOSPC denials  : 1
  missing file    : reported as an error
  cleanup         : 0 files, 0 bytes used
vfs exhaustion: PASS
```

`diskinfo` afterwards reports `files 0/32, created 5, deleted 5, enospc 1`.
Verdict: **PASS**.

### 3.6 `sync3` – deadlock and ring 3 fault recovery

```
deadlock demo: PASS
usermode: PASS (privilege transition ring0 -> ring3 -> ring0 via int 0x80)
[ring3] about to execute cli - this must fault (privileged instruction)
[ring3] FAULT in user mode: vector 13 (err=0x0, eip=0x10a2c0)
usermode: PASS (privilege transition ring0 -> ring3 -> ring0 via int 0x80)
```

After the fault the shell continues to answer `threads`, `scheduler` and
`memory`. Verdict: **PASS**.

### 3.7 `calc1` – scientific calculator

```
calc [rad]> 2+3*4
= 14
calc [rad]> sin(pi/2)
= 1
calc [rad]> 5!
= 120
calc [rad]> sqrt(2)
= 1.414213562
calc [rad]> 2^10
= 1024
calc [deg]> sin(90)
= 1
calc [deg]> tan(45)
= 1
calc [deg]> bin 255
0b11111111 = 255 decimal
calc [deg]> hex 255
0xff = 255 decimal
calc [deg]> ncr(5,2)
= 10
calc [deg]> npr(5,2)
= 20
calc [deg]> m+ 42
memory = 42
calc [deg]> mr
memory = 42
calc [deg]> 1/0
error: division by zero
calc [deg]> sqrt(-1)
error: sqrt domain error (negative argument)
calc [deg]> hist
  ... 8 history entries ...
calc [deg]> exit
calculator closed
```

Verdict: **PASS**.

### 3.8 `editor1` – editor and file manager

```
open file (leave empty for a new buffer): story.txt
'story.txt' does not exist yet - it will be created on save
edit:story.txt*> :l
  1 | Once upon a time there was a shark.
  2 | It swam in the kernel.
edit:story.txt*> :e 2 It swam in ring 0.
edit:story.txt*> :i 1 The SharkXOS chronicle
edit:story.txt*> :l
  1 | The SharkXOS chronicle
  2 | Once upon a time there was a shark.
  3 | It swam in ring 0.
edit:story.txt*> :w
saved 78 bytes to 'story.txt'
edit:story.txt> :q
sharkx> heapinfo
heap allocator state
  region          : 8192 KiB, 6 blocks (2 free)
  used / free     : 24960 / 8363648 bytes
  peak used       : 39360 bytes
  alloc / free    : 6 / 2
  [03] 0x132280  14368 bytes free
  [04] 0x135ac0    256 bytes used
sharkx> cat story.txt
...
sharkx> editor story.txt
loaded 'story.txt' (3 lines, 78 bytes)
edit:story.txt> :q!
files> ren story.txt tale.txt
renamed
files> cat tale.txt
...
files> rm tale.txt
deleted
file manager closed
```

The suite now brackets the editor with `heapinfo`. Before the editor the heap
held 24 672 bytes in three blocks; after the editor exited the peak of
39 360 bytes had fallen back and a 14 368-byte free block had appeared, i.e. the
editor's 6 KiB line array plus 8 KiB scratch buffer were returned and coalesced.
The residual +288 bytes is the VFS's 256-byte copy of `story.txt`, not an editor
leak. `:q!` on reload, rename, read-back and removal all behave correctly.

Verdict: **PASS**.

### 3.9 `monitor1` – system monitor

```
 SharkXOS system monitor
SYSTEM
  SharkXOS 1.0.0   x86 32-bit (IA-32)
  uptime 4.200 s   kernel ready   paging on (64 MiB identity)
SCHEDULER
  preemptive round robin, quantum 5 ticks (50 ms)
  ticks 420   context switches 90   preemptions 84   ready 1
  running: 2 (shell)   threads: 4   heartbeat: 3   memwatch: 1
THREADS
  id  name              state       cpu ticks  switches
  1   idle              READY       210        42
  2   shell             RUNNING     210        42  <- running
  3   heartbeat         WAITING     0          4
  4   memwatch          WAITING     0          2
MEMORY
  heap 8192 KiB: used 24 KiB, free 8167 KiB, peak 24 KiB
  allocations 3, frees 0, oom 0, double-free 0, invalid-free 0
  blocks 4 (1 free), largest free 8363904 bytes
STORAGE / SECURITY
  ramdisk 0/256 KiB, 0 files, 0 reads, 0 writes
  principal SYSTEM, access denials 0

  refreshing every second - press any key to return to the shell
system monitor closed
```

After returning, the shell handled `version`, `files`, `ls`, `touch`, `write`,
`cat` and `rm` normally. Verdict: **PASS**.

## 4. `make test`

```
[build] using cross compiler: i686-elf-g++
=== exit code 124 ; log: build/boot.log (162 lines) ===
  LOG  build/boot.log
```

Content checks in `build/boot.log`:

```
Kernel initialisation complete. 4 threads running.
  VGA read/write   : PASS
  PIT ticking      : PASS (2 ticks in ~20 ms)
thread states: PASS
memtest: PASS (8/8 checks)
```

Verdict: **PASS**.

## 5. Failures observed during development (now fixed)

These occurred in earlier runs and are recorded for honesty. None of them is
present in the final logs above.

| Failure | Root cause | Resolution |
|---|---|---|
| `lockorder` hung; mutex `order_1` owned by `order-B` after both threads terminated | direct handoff did not clear the waiter's self-deadlock path; dying thread kept ownership | added `Thread::granted`; `lock()` consumes it before the self-deadlock check; `mutex::release_all()` on exit |
| `threads` showed 3 threads / idle corrupted at boot | `adopt_boot_thread()` overwrote slot 0 | use `alloc_slot()` |
| thread/scheduler tables misaligned, `%llu` wrong | formatter ignored flags and mis-parsed `ll` | rewrote `kvsnprintf` field rendering |
| `touch` printed `rwd r--` even after permission changes | output was synthesised, not read back | read the created file and print stored masks |
| two permission masks in one line printed identically | single static buffer in `perm_string()` | rotate four buffers |
| long command scripts lost characters | 256-byte input queue | raised to 512 bytes |
| tests failed with `Could not open build/sharkxos.iso` | `make clean` deleted the ISO before the run | rebuild first; logs moved to `tests/logs/` |
| `make run` failed in WSL | no X display | headless default, `GUI=1` optional |
| link warning about TEXTREL/PIE | host default PIE | `-static -no-pie` |

## 6. What the evidence does and does not show

**Shows:**

* the kernel boots from the delivered ISO through GRUB/Multiboot 2;
* every required subsystem executes on the real kernel, including failure paths
  (`ENOSPC`, `ENOENT`, double free, invalid free, OOM, deadlock, #GP in ring 3);
* the computed verdicts from self-checking commands are `PASS`;
* the applications read and write real kernel state;
* the build targets work in the tested environment.

**Does not show:**

* security against hostile ring 3 code (there is none; see `KNOWN_ISSUES.md`);
* behaviour on real hardware, SMP, or persistent storage;
* long-duration stability beyond the ~40–60 s per suite;
* graphical output, because tests run headless and use the serial mirror. The VGA
  path itself is checked by `diag` (write/read-back of a video cell).

## 7. Reproduction

```bash
make clean
make
tools/run_all_tests.sh
```

Expected tail:

```
ALL REQUESTED SUITES PASSED (logs in tests/logs/)
```

Individual suite:

```bash
tools/run_all_tests.sh smoke1
```

Raw logs: `tests/logs/smoke1.log`, `smoke2.log`, `vfs2.log`, `sync3.log`,
`states1.log`, `calc1.log`, `editor1.log`, `monitor1.log`.
