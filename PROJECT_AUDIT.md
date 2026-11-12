# SharkXOS – Project Audit

This document checks the delivered system against the original assignment. Each
requirement lists where it is implemented, how it was verified, and an honest
verdict. Verification refers to the serial logs captured by
`tools/run_all_tests.sh` into `tests/logs/` and summarised in
[TEST_RESULTS.md](TEST_RESULTS.md).

Legend: **Met** = implemented and observed working; **Partial** = works but with
a documented limitation; **Not met** = missing.

---

## 1. Platform and toolchain constraints

| # | Requirement | Implementation | Evidence | Verdict |
|---|---|---|---|---|
| 1.1 | Strict IA-32, 32-bit protected mode, no long mode | `-march=i686`, ELF32, Multiboot2; no x86_64 code, no long-mode paging | `file build/kernel.elf` → `ELF 32-bit LSB executable, Intel 80386`; `readelf -h` → `ELF32`, `EXEC` | Met |
| 1.2 | Freestanding C++ and NASM | `-ffreestanding -fno-exceptions -fno-rtti -fno-builtin -fno-stack-protector`; own string/math library; NASM entry/ISR stubs | build log; `lib/string.cpp`, `lib/kmath.cpp`; no `#include <stdio.h>` etc. | Met |
| 1.3 | No host libc, no `malloc` | All allocation through `heap::kmalloc`; linker uses `-nostdlib -static -no-pie` | link step succeeds with `-nostdlib`; heap stats in `memory` | Met |
| 1.4 | Boot with GRUB2 / Multiboot2 | Multiboot2 header in `boot/entry.asm`, parsed by `kernel/multiboot.cpp` | `grub-file --is-x86-multiboot2` OK; boot log prints `multiboot2 handoff ok` | Met |
| 1.5 | `i686-elf` cross toolchain | GNU GCC 15.2.0 / binutils 2.45 installed at `/opt/cross/i686-elf`, symlinked into `/usr/local/bin` | `[build] using cross compiler: i686-elf-g++`; `version` prints `gcc 15.2.0` | Met |
| 1.6 | QEMU test target | `make run`, `make debug`, headless serial harness | `make run` boots to `sharkx>`; `make debug` opens GDB stub `:1234` | Met |

## 2. Boot and CLI shell

| # | Requirement | Implementation | Evidence | Verdict |
|---|---|---|---|---|
| 2.1 | Boot sequence initialises hardware | `kernel/main.cpp`: GDT/TSS → IDT → PIC → PIT → paging → heap → VFS → scheduler → shell | boot log banner + `diag` | Met |
| 2.2 | Prompt `sharkx>` | `kernel/shell.cpp` | every log line prompts with `sharkx>` | Met |
| 2.3 | At least three custom commands | 37 registered commands (11 built-ins, 4 apps, 22 subsystem commands) | `help` prints the full table | Met |
| 2.4 | Commands are real, not canned | All commands read/modify kernel state; demos allocate threads and mutexes | VFS demo changes `ls`/`diskinfo`; race counter changes; `memtest` mutates heap | Met |
| 2.5 | Line editing | Backspace, Ctrl-C (cancel line) and Ctrl-L (clear screen) are handled; there is no arrow-key command history | code in `shell.cpp`; interactive use | Partial |
| 2.6 | Shutdown / reboot | ACPI-free QEMU shutdown via `outw(0x604, 0x2000)` / keyboard controller reset | `shutdown` powers QEMU off; `reboot` resets | Met |

## 3. Threads and scheduling

| # | Requirement | Implementation | Evidence | Verdict |
|---|---|---|---|---|
| 3.1 | Thread object with five states | `enum class ThreadState { NEW, READY, RUNNING, WAITING, TERMINATED }`; `Thread` array of 24 | `states` demo | Met |
| 3.2 | NEW state observable | `thread::create(..., start_ready=false)` leaves thread NEW; `thread::start()` moves it to READY | `states` prints `observed: NEW READY RUNNING WAITING TERMINATED` | Met |
| 3.3 | At least two kernel threads | idle, shell, heartbeat, memwatch are created at boot | boot log `4 threads running`; `threads` lists them | Met |
| 3.4 | Preemptive round-robin scheduler | PIT tick every 10 ms, 5-tick quantum, ready queue, preemption counter | `scheduler` shows ticks, context switches, preemptions; `race` interleaves | Met |
| 3.5 | Real context switching | `isr_common` passes `Registers*` to `interrupt_dispatch`; scheduler returns the next frame; `mov esp,eax` + pop + `iret` | heartbeat and race threads interleave with the shell; context-switch counter grows | Met |
| 3.6 | Blocking and waking | `thread::sleep_ms`, mutex wait queue, `WAITING` state | `threads` shows heartbeat/memwatch WAITING; `states` shows WAITING on a mutex | Met |
| 3.7 | Clean termination | `thread::exit` marks TERMINATED, releases owned mutexes, yields; reaper frees stacks | `race`/`lockorder` demo threads end TERMINATED; `threads` shows it | Met |

## 4. Synchronisation

| # | Requirement | Implementation | Evidence | Verdict |
|---|---|---|---|---|
| 4.1 | Mutex | `sync/mutex.cpp`: blocking lock, recursive depth, direct handoff, wait queue, `release_all` on exit | `race 100` with mutex yields 200 | Met |
| 4.2 | Semaphore | Counting semaphore (`semaphore::init/wait/signal`) built on the scheduler wait queue; `semtest` runs two blocking consumers | `semtest`: both WAITING, one signal releases exactly one, second releases the other, final count 0, `PASS` | Met |
| 4.3 | Race-condition demonstration | `race` runs unsynchronised then synchronised increments with a forced yield inside the critical update | log: without mutex counter < expected, with mutex exact; `race test: PASS` | Met |
| 4.4 | Deadlock demonstration | `deadlock`: two threads take two mutexes in opposite order | log: `circular wait`, `wait-for cycle found`, recovery | Met |
| 4.5 | Deadlock prevention | (a) wait-for cycle detection aborts acquisition; (b) global lock ordering demo | `deadlock demo: PASS`; `lock ordering: PASS (no deadlock)`, counter 200 | Met |
| 4.6 | No lost wakeups / ownership bugs | `granted` handoff flag, recursive-depth reset, mutex release on thread death | `lockorder` repeated runs end with owner `none` | Met |

## 5. Kernel heap

| # | Requirement | Implementation | Evidence | Verdict |
|---|---|---|---|---|
| 5.1 | `kmalloc` / `kfree` | 8 MiB static region, 16-byte aligned boundary-tagged blocks | `heapinfo`, `memory` | Met |
| 5.2 | Extra API | `kzalloc`, `krealloc`, `validate_ptr` | `memtest` covers realloc data preservation | Met |
| 5.3 | Statistics | used/free/peak, block counts, largest free, alloc/free counts, coalesces | `heapinfo` | Met |
| 5.4 | Out-of-memory handling | allocation over capacity returns `nullptr`, increments `fail_count` | `memtest` check `out of memory handled` | Met |
| 5.5 | Double-free detection | blocks marked `HEAP_MAGIC_DEAD`; second free rejected and counted | `[PASS] double free rejected` | Met |
| 5.6 | Invalid-free detection | pointer range + magic + tag validation | `[PASS] invalid free rejected` | Met |
| 5.7 | Coalescing | adjacent free blocks merged; `coalesce_count` | `[PASS] free list coalescing` | Met |
| 5.8 | Concurrency safety | heap operations wrapped in `kernel_lock()` | race/memwatch threads do not corrupt heap | Met |

## 6. RAM-disk VFS

| # | Requirement | Implementation | Evidence | Verdict |
|---|---|---|---|---|
| 6.1 | RAM disk | 256 KiB fixed region, 32 file slots, 64 KiB max file | `diskinfo` | Met |
| 6.2 | CRUD | `create/remove/read/write/truncate/rename`, append mode | VFS demo + editor round-trip | Met |
| 6.3 | Shell commands | `ls touch cat write rm diskinfo` | smoke2 log | Met |
| 6.4 | Capacity handling | `VFS_ENOSPC` when the disk is full; no partial corruption | `vfstest`: 4×64 KiB accepted, 5th rejected, cleanup 0 bytes | Met |
| 6.5 | Missing file handling | `VFS_ENOENT` | `vfstest` reports missing file | Met |
| 6.6 | Metadata | owner, permissions, size, created/modified ticks, read/write counters | `ls`, `permissions`, `info` | Met |

## 7. Access control

| # | Requirement | Implementation | Evidence | Verdict |
|---|---|---|---|---|
| 7.1 | Access model | Logical principals `SYSTEM`, `USER`, `RESTRICTED`; per-file user/restricted r/w/d bits | `access_test`: 7/7 checks | Met |
| 7.2 | `whoami` | prints current principal and counters | smoke2 log | Met |
| 7.3 | `permissions` | prints owner, per-principal bits, model note | smoke2 log | Met |
| 7.4 | `access_test` | exercises allowed/denied read, write, delete for all principals | `access_test: PASS (all 7 checks)` | Met |
| 7.5 | `su` | switches logical principal | `now acting as USER` / `RESTRICTED` | Met |
| 7.6 | Ring 3 | 64 MiB identity-mapped U/S pages, TSS `esp0`, syscalls `write/putc/ticks/whoami/exit`, fault recovery | `usermode` prints from CPL=3 and exits 0; `usermode fault` survives `cli` (vector 13) | Partial – see 7.7 |
| 7.7 | True hardware isolation | Not achieved: user pages share the identity map with kernel memory, so ring 3 can read/write kernel addresses | documented in [KNOWN_ISSUES.md](KNOWN_ISSUES.md) | Partial |

The assignment explicitly allows an honest "no real ring 3" statement. SharkXOS
goes further and demonstrates a real CPL=3 transition with syscalls and fault
recovery, while documenting that the page tables are not a security boundary.

## 8. Applications

| # | Requirement | Implementation | Evidence | Verdict |
|---|---|---|---|---|
| 8.1 | Scientific calculator | Recursive-descent parser, full operator precedence, functions, factorial, degrees/radians, base conversion, memory, history, nCr/nPr | `tests/calc1.txt` log: 14, 1, 120, 1.414213562, 1024, DEG results, bin/hex, nCr/nPr, errors | Met |
| 8.2 | Text editor | VFS-backed line editor with load/save, insert/edit/delete/append, status, unsaved-changes warning; 64×96 line array and 8 KiB scratch buffer come from `kmalloc` and are freed on exit | `tests/editor1.txt` log: 3 lines, saved 78 bytes, reload, `:q!`, and `heapinfo` shows the 14 368-byte block returned after exit | Met |
| 8.3 | File manager | `ls cat touch write rm ren info df exit` over the VFS | `tests/editor1.txt` log | Met |
| 8.4 | System monitor | live uptime, scheduler, threads, heap, RAM disk, principal | `tests/monitor1.txt` log | Met |
| 8.5 | Apps use real kernel data | Apps call `vfs::`, `heap::`, `scheduler::`, `time::` directly | source inspection; monitor values change between refreshes | Met |

## 9. Build system

| # | Requirement | Implementation | Evidence | Verdict |
|---|---|---|---|---|
| 9.1 | `make` | builds objects, links ELF32, checks format, creates ISO | `make clean && make` succeeds | Met |
| 9.2 | `make clean` | removes `build/` | verified | Met |
| 9.3 | `make iso` | `grub-mkrescue` produces `build/sharkxos.iso` | verified, 5.5 MB | Met |
| 9.4 | `make run` | headless QEMU by default, `GUI=1` for a window | boots to shell | Met |
| 9.5 | `make debug` | QEMU paused with GDB stub on `:1234` (`-s -S`) | verified | Met |
| 9.6 | Cross-compiler auto-detection | uses `i686-elf-*` when present, falls back to host `gcc -m32` | build banner | Met |
| 9.7 | Extra: `make test` | boots the ISO and runs the smoke suite | added | Met |

## 10. Documentation

| # | Requirement | File | Verdict |
|---|---|---|---|
| 10.1 | Project overview | `README.md` | Met |
| 10.2 | Audit | `PROJECT_AUDIT.md` | Met |
| 10.3 | Architecture | `ARCHITECTURE.md` | Met |
| 10.4 | Development plan | `DEVELOPMENT_PLAN.md` | Met |
| 10.5 | Build instructions | `BUILD_INSTRUCTIONS.md` | Met |
| 10.6 | Test plan | `TEST_PLAN.md` | Met |
| 10.7 | Test results | `TEST_RESULTS.md` | Met |
| 10.8 | Known issues | `KNOWN_ISSUES.md` | Met |
| 10.9 | Changelog | `CHANGELOG.md` | Met |
| 10.10 | Academic report, 10–15 pages, chapters I–IV, AI disclosure | `REPORT.md` | Met |

## 11. Deviations and honest gaps

1. **No separate user address space.** Ring 3 runs against the kernel's identity
   map. The privilege transition, TSS stack switch, syscall gate and fault
   recovery are real; memory isolation is not. This is the largest gap.
2. **Logical access control.** `su` changes a kernel variable. It demonstrates
   the permission logic and denial paths but is not a security boundary.
3. **Single CPU.** No SMP, no per-CPU data, no APIC. The scheduler uses global
   `cli`/`sti` for kernel critical sections.
4. **No persistent storage.** The VFS lives in RAM and is lost on reset.
5. **Fixed-size kernel structures.** 24 thread slots, 32 file slots and 16 mutex
   slots are compile-time limits; each semaphore has 8 waiter slots and must be
   provided by the caller.
6. **No shell command history.** Only backspace, Ctrl-C and Ctrl-L line editing
   are implemented.
7. **Monitor refresh is blocking.** It sleeps between samples instead of using a
   timer callback; the shell is unavailable while the monitor is open.
8. **No user program loader.** The ring 3 payload is a compiled-in kernel blob.
9. **The repository is not under version control.** All files are plain
   uncommitted files in the working tree.
10. **Some warnings remain** in the build (unused helper functions in
    `heap.cpp`/`string.cpp`). They are harmless but visible.
11. **Test coverage is script-driven.** The automated suites exercise the happy
    paths plus the important error paths; they are not exhaustive fuzzing.

## 12. Conclusion

All mandatory subsystems are present and were exercised on the real kernel in
QEMU. The assignment's "no fake output" rule is respected: demos manipulate
real threads, mutexes, heap blocks and files, and failure paths (`ENOSPC`,
double free, invalid free, deadlock, ring 3 fault) are triggered on purpose and
reported truthfully. The principal remaining weaknesses are the coarse ring 3
mapping and the absence of persistent storage, both documented above and in
`KNOWN_ISSUES.md`.
