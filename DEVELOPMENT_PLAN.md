# SharkXOS – Development Plan

This document records how the project was planned and executed, what changed
along the way, and why. It is written after the fact from the actual work done;
it is not a retroactive wish list.

---

## 1. Starting point

The assignment specified an empty project directory. The initial state was
therefore:

* no source files, no build system, no documentation;
* WSL Ubuntu 24.04 with `build-essential`, `nasm`, GRUB tools and QEMU either
  missing or partially installed;
* no `i686-elf` cross compiler on `PATH`.

The first milestone was simply to get a Multiboot2 "hello" kernel to boot in
QEMU, because every later subsystem depends on that path.

## 2. Phase plan

| Phase | Goal | Key deliverables | Status |
|---|---|---|---|
| P0 | Toolchain and skeleton | apt packages, cross compiler, `Makefile`, linker script, Multiboot2 header, `kmain`, VGA console, `kprintf` | done |
| P1 | Descriptor tables and interrupts | GDT, TSS, IDT, PIC remap, exception stubs, PIT, keyboard IRQ | done |
| P2 | Heap | 8 MiB arena, `kmalloc/kfree`, split/coalesce, magic checks, stats, self-test | done |
| P3 | Threads and scheduling | `Thread`, 5 states, context switch via interrupt frame, round robin, idle thread, sleep | done |
| P4 | Synchronisation | mutex with wait queue, semaphore, race demo, deadlock demo, cycle detection, lock ordering | done |
| P5 | VFS and security | RAM disk, CRUD, permissions, `su`, `access_test` | done |
| P6 | Shell and commands | prompt, parser, core commands, subsystem commands, shutdown/reboot | done |
| P7 | Applications | calculator, editor, file manager, monitor | done |
| P8 | Ring 3 | identity paging, TSS `esp0`, syscall gate, payload, fault recovery | done (coarse isolation) |
| P9 | Verification | serial test harness, suites, regression runs, captured logs | done |
| P10 | Documentation | all required documents plus this report | done |

## 3. Execution notes per phase

### P0 – Toolchain

HTTP apt sources were blocked in the environment; switching the Ubuntu sources
to HTTPS allowed the packages to install. A prebuilt `i686-elf` toolchain
(GCC 15.2.0, binutils 2.45) was unpacked into `/opt/cross/i686-elf` and
symlinked into `/usr/local/bin`. The Makefile detects it and prints
`[build] using cross compiler: i686-elf-g++`; a host `gcc -m32` fallback
remains for machines without the cross compiler.

Linking initially produced a PIE/TEXTREL warning. The link line was changed to
`-static -no-pie` and the kernel verified as `ELF32`, `EXEC`, `Intel 80386`,
with a valid Multiboot2 header.

### P1 – Interrupts

The GDT/TSS and IDT worked on the first boot. The tricky part was the shared
ISR path: the design decision to let `interrupt_dispatch` return a frame pointer
meant that timer preemption, `int 0x81` yield and `int 0x80` syscalls all use
one assembly path. This was chosen deliberately, because writing three separate
switch routines invites subtle stack-layout bugs.

### P2 – Heap

The allocator was written with boundary tags, a checksum per header, and a
`DEAD` magic for freed blocks. The self-test was written at the same time as the
allocator, which made it possible to catch coalescing and double-free mistakes
immediately. It reports 8/8 checks.

### P3 – Threads

Two real bugs surfaced here:

1. `adopt_boot_thread()` bypassed `alloc_slot()` and overwrote slot 0, which
   corrupted the idle thread. Fixed by using the normal slot allocator; the boot
   log now reports 4 threads (idle, shell, heartbeat, memwatch).
2. `states` was added late to make the NEW state observable. Threads are created
   with `start_ready=false`, displayed as NEW, then explicitly started.

### P4 – Synchronisation

The most instructive bug of the project. `unlock()` handed the mutex directly to
a waiter, but the waiter's `lock()` then entered the "already owned by me"
branch, treated it as a self-deadlock, and never re-acquired. The mutex stayed
owned by a thread that later terminated, so a subsequent `lockorder` run hung
with `owner=order-B`.

Fix: add `Thread::granted`. `unlock()` sets `next->granted = m`; `lock()`
consumes that flag before the self-deadlock check. In addition,
`thread::exit()` calls `mutex::release_all()`, so a dead thread can never keep a
lock. `lockorder` then passed with the expected counter of 200.

A second, smaller bug: `perm_string()` returned a pointer to one static buffer,
so printing two permission masks in one `kprintf` showed the same value twice.
Fixed with four rotating buffers.

Later, while adding a semaphore test, a third synchronisation bug surfaced:
`signal()` handed a permit to a waiter without incrementing the count, so the
woken consumer saw a count of zero and blocked again. `signal()` now publishes
the permit before waking, and `semtest` exercises two consumers released one
signal at a time.

### P5 – VFS

The VFS was straightforward, but the assignment's "no fake output" rule pushed
us to add `vfstest`, which fills the RAM disk with four 64 KiB files, verifies
that the fifth write returns `ENOSPC`, verifies a missing-file read returns
`ENOENT`, and cleans up. `disk info` then shows the real `enospc` counter.

### P6 – Shell

The formatter was rewritten once during this phase. The first version ignored
`-` (left align) and `0` (zero pad) correctly in all combinations and mishandled
`%llu`, which made the thread and scheduler tables ragged. The rewrite separates
field rendering (`emit_field`) from number conversion, supports `- 0 +`, width,
precision and `l/ll/z`, and fixed the tables.

### P7 – Applications

The calculator parser and the editor were the largest application efforts. The
calculator uses a hand-written recursive-descent parser rather than a
shunting-yard, because function calls (`sin(...)`, `ncr(a,b)`) are natural in
recursive descent. The editor stores lines on the heap and writes through the
VFS; its dirty flag triggers the unsaved-changes warning on `:q`.

### P8 – Ring 3

The CPU privilege transition required paging (for the U/S bit), a TSS `esp0`,
and an `iret` frame with user selectors. The first attempts faulted because the
TSS was not loaded and because the page tables lacked the user bit. After that,
the payload ran at CPL=3 and used `int 0x80` successfully. The `cli` test
produced the expected #GP; `user_mode_abort()` catches it and returns to the
shell.

The limitation is explicit: identity-mapping 64 MiB with the user bit means
kernel memory remains user-accessible. Building per-process page tables and a
user heap was out of scope for the remaining time.

### P9 – Verification

A headless test harness was built around QEMU's `-serial stdio`, because WSL has
no display. Console output is mirrored to COM1 and COM1 input is injected into
the keyboard queue, so the exact same kernel can be tested without a GUI. Eight
command scripts cover boot/diagnostics, VFS/security/sync, thread states,
calculator, editor/file manager and the monitor. `tools/run_all_tests.sh`
checks explicit expected strings, writes logs to `tests/logs/` and returns a
non-zero exit code on failure.

### P10 – Documentation

All documents were written from the implementation and the captured logs. Where
something is incomplete (ring 3 isolation, persistent storage, shell history),
it is listed as a limitation instead of being described as finished.

## 4. Deviations from the original plan

| Planned | Actual | Reason |
|---|---|---|
| Physical frame allocator | Not built; static heap arena | Time budget; identity map suffices for this feature set |
| Per-process address space | Single identity-mapped space | Ring 3 demo works without it; true isolation documented as missing |
| User program loader | Payload compiled into the kernel | No ELF loader or user filesystem yet |
| Shell history with arrow keys | Backspace/Ctrl-C/Ctrl-L only | Keyboard driver does not decode extended scancodes |
| Persistent disk | RAM disk only | No ATA/ATA-PIO driver |
| SMP scheduler | Single CPU | PIC/PIT only, no APIC |
| More than 32 files / 256 KiB | Fixed limits | Static structures keep the code small and auditable |

## 5. What would come next

If the project continued, the priority order would be:

1. Physical frame allocator + per-process page directories, so ring 3 has real
   isolation and user stacks/heaps are separate.
2. A simple ELF32 loader and a `exec` syscall, so applications can live in the
   VFS instead of the kernel image.
3. ATA PIO block driver and an on-disk file system for persistence.
4. Extended keyboard scancodes (arrows, function keys) and shell history.
5. Priority scheduling and a sleep queue instead of scanning all threads per
   tick.
6. Automated tests for fault injection (corrupt heap headers, full waiter
   queues) and a longer soak test.

## 6. Effort summary

The work was iterative: build, boot, read the serial log, fix, repeat. The
phases above were not strictly sequential — for example, the heap self-test was
extended while testing the scheduler, and the VFS exhaustion test was added
while writing the editor. The final tree contains about 6 500 lines of kernel
code, 8 test scripts, 3 test harnesses and 10 documents.
