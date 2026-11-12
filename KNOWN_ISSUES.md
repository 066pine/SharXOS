# SharkXOS – Known Issues and Limitations

This is the honest list of what does not work, what is fragile, and what is
simply missing. Nothing here is hidden in the test results; the corresponding
test evidence is in [TEST_RESULTS.md](TEST_RESULTS.md).

Severity: **High** = affects a stated requirement or security claim; **Medium** =
limits usability or robustness; **Low** = cosmetic or narrow edge case.

---

## 1. Security and isolation

### KI-1 (High) – Ring 3 has no memory isolation

The page tables identity-map the first 64 MiB with the user bit set (P|RW|US) and
there is a single page directory. Ring 3 code therefore runs at CPL=3 and cannot
execute privileged instructions (I/O, `cli`, `hlt`, control registers), but it
**can read and write kernel memory**.

Impact: the ring 3 demo proves a real privilege transition, syscalls and fault
recovery, but it is not a security boundary.

Possible fix: per-process page directories, a physical frame allocator, user
heaps/stacks mapped only in their own address space, and a copy-in/copy-out
syscall ABI.

### KI-2 (High) – Access control is logical, not enforced by hardware

`su` changes a variable in the kernel. File permission checks are performed by
kernel code in ring 0; a malicious ring 3 payload could bypass them entirely
because of KI-1.

Impact: the permission matrix and denial paths are real code and are tested, but
they do not protect against hostile code.

### KI-3 (Medium) – `user_mode_abort` is a recovery heuristic

A ring 3 fault is handled by rewriting the saved interrupt frame to return to
ring 0. This works for the tested `#GP` from `cli`, but it does not tear down any
user state (there is none) and a fault raised while the kernel is already in a
bad state could still panic. It is a demo mechanism, not a full fault handler.

## 2. Memory

### KI-4 (Medium) – Heap is a fixed 8 MiB BSS arena

There is no physical frame allocator and no way to grow the heap. When the arena
is exhausted, allocations return `nullptr` (handled and counted), but long-running
sessions that create many threads/files can reach that point.

### KI-5 (Medium) – Terminated thread slots are not reclaimed

`thread::create` only reuses slots whose `id == 0`. A terminated thread keeps its
slot (its stack is freed by the reaper, but the `Thread` entry remains). After 24
thread creations in one boot, `create` fails and demos print an error. A reboot
resets the table.

Impact: repeated `states`, `race`, `deadlock` or `lockorder` runs eventually
exhaust the table. The failure is graceful.

Possible fix: a generation counter and reuse of TERMINATED slots once no command
can still reference them, or a proper join/reap API.

### KI-6 (Low) – No FPU state save on context switch

The kernel is built with `-mno-sse -mno-sse2 -mno-mmx`, but `double` arithmetic
uses the x87 stack. Threads do not save/restore x87 state, so if two threads used
floating point simultaneously their results could interfere. In practice only
the shell/calculator thread performs FP work; the other threads are integer-only.

## 3. Scheduling and concurrency

### KI-7 (Medium) – Single CPU only

No SMP, no APIC, no per-CPU data. `kernel_lock()` is a global `cli`/`sti` depth
counter, and the scheduler assumes one CPU.

### KI-8 (Medium) – Sleep wakeups are scanned, not queued

Every timer tick scans all 24 thread slots to find expired sleepers. This is fine
at this scale but is O(threads) per tick.

### KI-9 (Low) – Mutex waiter queue is fixed at 8

A ninth thread trying to wait on the same mutex is not enqueued (`enqueue_waiter`
silently drops it). At the current thread limit this is hard to trigger, but it
is not a robust blocking primitive.

### KI-10 (Low) – Deadlock detection is bounded

`would_deadlock()` walks the wait-for chain with a guard of 64 hops. A longer
cycle would not be detected. With 24 threads and 16 mutexes the bound is above
the reachable cycle length, but it is still a heuristic.

### KI-11 (Low) – Preemption is not disabled inside every critical section

Most shared state uses `kernel_lock()`, but the shell and applications read
statistics without it. Values can be momentarily inconsistent (for example a
tick counter incrementing during a print). This is cosmetic, not corrupting.

## 4. Storage

### KI-12 (High) – No persistence

The RAM disk lives in a static buffer and is lost on reset. There is no ATA,
AHCI, USB or network driver.

### KI-13 (Medium) – Fixed VFS limits

32 files, 256 KiB total, 64 KiB per file, 31-character names, no directories, no
symlinks, no timestamps beyond boot-relative ticks. `VFS_ENOSPC` is returned
cleanly when full.

### KI-14 (Low) – File data is heap-backed and not defragmented

Files grow by reallocating heap buffers. Repeated create/delete of large files
can fragment the heap even though the free list coalesces adjacent blocks.

### KI-15 (Low) – Editor limits

The editor holds 64 lines of 96 characters and reads at most 8 KiB per file. Both
buffers (the line array and the file scratch buffer) are allocated from the
kernel heap when the editor starts and freed when it exits, so the limits are
heap-backed rather than static. A larger file is truncated on load; the status
command shows the approximate size. There is no undo, search or cursor movement
(it is a line editor).

## 5. Shell and user interface

### KI-16 (Medium) – No command history or arrow keys

The keyboard driver recognises `0xE0` extended scancodes but deliberately ignores
them. The shell supports backspace, Ctrl-C and Ctrl-L only.

### KI-17 (Low) – No quoting, redirection or pipes

Arguments are split on whitespace. `echo "a b"` prints the quotes as separate
tokens. There are no `>`, `|`, `&` or environment variables.

### KI-18 (Low) – Serial input queue is 512 bytes

Very long pasted command scripts can overflow the keyboard queue; bytes are
dropped. Automated tests keep command files short enough to avoid this.

### KI-19 (Low) – Monitor refresh is blocking

`monitor` sleeps one second between samples inside the app loop. While it is open
the shell does not run (other kernel threads still do). "Press any key" is
implemented by polling the keyboard after the sleep.

### KI-20 (Low) – Shutdown is QEMU-specific

`shutdown` writes ACPI/PM ports used by QEMU (`0x604`, `0xB004`, `0x4004`). On
real hardware it may not power off; `reboot` uses the keyboard controller reset
line.

## 6. Build and tooling

### KI-21 (Low) – Compiler warnings remain

The build is not warning-free. Typical warnings:

* `memory/heap.cpp`: `insert_after` and `unlink` are defined but unused after the
  allocator was simplified to address-ordered traversal;
* `lib/string.cpp`: a helper (e.g. `reverse_str`) is unused.

They do not affect correctness. A stricter build would remove or use them.

### KI-22 (Low) – The project is not a git repository

There is no commit history, no tags and no `.gitignore`. All files are plain
files in the working tree. `build/` and `tests/logs/` are generated artifacts.

### KI-23 (Low) – Host toolchain fallback is less reproducible

If `i686-elf-g++` is absent, the Makefile falls back to host `g++ -m32`. That
works on the tested host but depends on multilib packages and host header
availability. The cross compiler is the supported path.

### KI-24 (Low) – No continuous integration

Tests are run manually through `tools/run_all_tests.sh`. There is no CI
configuration, no coverage measurement and no automatic regression gate.

## 7. Things that are *not* bugs

* QEMU exit code `124` during tests: the harness kills QEMU with `timeout`.
* `make clean` deletes `build/`, including previously captured logs; test logs
  are now written to `tests/logs/` so they survive.
* A thread shown as `TERMINATED` in `threads` is expected after a demo; see
  KI-5 for slot reuse.
* `permissions` shows `r--` for `RESTRICTED` on a newly created file; that is
  the intended default (`user = rwd`, `restricted = r--`).

## 8. Summary table

| ID | Severity | Area | Short description |
|---|---|---|---|
| KI-1 | High | security | no ring 3 memory isolation |
| KI-2 | High | security | logical principals only |
| KI-3 | Medium | security | fault recovery is a demo heuristic |
| KI-4 | Medium | memory | fixed 8 MiB heap |
| KI-5 | Medium | threads | terminated slots never reclaimed |
| KI-6 | Low | threads | no x87 state save |
| KI-7 | Medium | scheduler | uniprocessor only |
| KI-8 | Medium | scheduler | O(n) sleep scan per tick |
| KI-9 | Low | sync | 8 waiters per mutex |
| KI-10 | Low | sync | bounded cycle walk |
| KI-11 | Low | sync | unlocked reads of statistics |
| KI-12 | High | storage | no persistence |
| KI-13 | Medium | VFS | fixed limits, no directories |
| KI-14 | Low | VFS | heap fragmentation |
| KI-15 | Low | editor | 64×96 line buffer, 8 KiB read |
| KI-16 | Medium | shell | no history/arrows |
| KI-17 | Low | shell | no quoting/pipes |
| KI-18 | Low | input | 512-byte serial queue |
| KI-19 | Low | apps | blocking monitor refresh |
| KI-20 | Low | power | QEMU-specific shutdown |
| KI-21 | Low | build | warnings remain |
| KI-22 | Low | repo | not under version control |
| KI-23 | Low | build | host fallback less reproducible |
| KI-24 | Low | process | no CI |
