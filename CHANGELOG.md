# Changelog

All notable changes to SharkXOS. The project is a single 1.0.0 delivery; the
entries below record the work that produced it, including the bugs that were
found during testing.

## [1.0.0] – 2026-11-12

### Added

* Multiboot2 boot path with GRUB 2, 32-bit protected mode entry, kernel loaded
  at 1 MiB, verified `ELF32 / EXEC / Intel 80386`.
* GDT with kernel/user selectors, TSS (`esp0`), IDT with 256 gates, PIC remap to
  `0x20–0x2f`, exception handlers and IRQ stubs.
* PIT at 100 Hz, monotonic tick counter, uptime in milliseconds, blocking and
  busy-wait sleep.
* VGA text console (80×25, colours) and a freestanding `kprintf` with
  `%d %i %u %x %X %p %c %s %f`, `- 0 +` flags, width, precision and
  `l`/`ll`/`z` modifiers.
* Serial console on COM1 (115200 8N1) mirroring VGA output, with received bytes
  injected into the keyboard queue for headless operation.
* PS/2 keyboard driver (scancode set 1, shift/caps/ctrl, 512-byte input queue).
* Kernel heap: 8 MiB static arena, 32-byte boundary-tagged headers, 16-byte
  alignment, split/coalesce, `kmalloc/kzalloc/krealloc/kfree`, validation,
  OOM handling, double-free and invalid-free detection, full statistics and an
  8-check self-test.
* Threads: `Thread` object with NEW/READY/RUNNING/WAITING/TERMINATED, stack
  allocation, fabricated initial interrupt frame, YIELD gate `int 0x81`.
* Preemptive round-robin scheduler with 5-tick quantum, ready queue, idle thread,
  sleep/wake, reaping of terminated stacks and statistics.
* Boot threads: `idle`, `shell`, `heartbeat`, `memwatch`.
* Mutexes with FIFO waiters, direct ownership handoff, recursion, self-deadlock
  rejection, wait-for cycle detection, `lock_ordered`, and release on thread
  exit. Counting semaphore built on the same wait queue.
* Demonstrations: `race` (unsynchronised vs mutex), `deadlock` (two-mutex cycle
  with detection and recovery), `lockorder` (prevention), `states` (all five
  thread states), `semtest` (two consumers, one permit at a time).
* RAM-disk VFS: 32 files, 256 KiB, 64 KiB max per file, create/remove/read/
  write/truncate/rename, append, permissions, metadata, statistics, `ENOSPC` and
  `ENOENT` handling.
* Access control: `SYSTEM`/`USER`/`RESTRICTED` principals, per-file r/w/d masks,
  `whoami`, `su`, `permissions`, `access_test`.
* Ring 3 demo: 64 MiB identity paging with user bit, TSS stack switch, `int 0x80`
  syscalls (`write`, `putc`, `ticks`, `whoami`, `exit`), fault recovery for
  privileged instructions.
* Shell with 37 registered commands, prompt `sharkx>`, argument parser,
  shutdown/reboot, and `help` listing commands with aligned descriptions.
* Scientific calculator: recursive-descent parser, functions, factorial, powers,
  constants, nCr/nPr, DEG/RAD, bin/dec/hex, memory and history.
* Text editor: VFS-backed line editor with insert/edit/delete/append/list, save,
  status, dirty flag and unsaved-changes warning. Its 64×96 line buffer and
  8 KiB file scratch buffer are allocated from the kernel heap on entry and
  freed on exit.
* File manager: `ls cat touch write rm ren info df`.
* System monitor: live uptime, scheduler, thread table, heap, RAM disk and
  principal.
* `vfstest`: fills the RAM disk with 4×64 KiB, verifies the fifth allocation
  fails with `ENOSPC`, verifies missing-file handling, then cleans up.
* Build system: cross-compiler auto-detection, host fallback, `all`, `clean`,
  `iso`, `run`, `debug`, `check`, `test`, `help`; `run` is headless by default
  and `GUI=1` opens a window.
* Test harnesses: `tools/boot_test.sh`, `tools/long_test.sh`,
  `tools/run_all_tests.sh`; eight command scripts and captured logs under
  `tests/`.
* Documentation: README, project audit, architecture, development plan, build
  instructions, test plan, test results, known issues, changelog and the
  academic report.

### Fixed

* **Boot thread slot corruption**: `adopt_boot_thread()` wrote into slot 0 and
  overwrote the idle thread. It now allocates a free slot; the kernel reports
  4 threads at boot.
* **Mutex handoff lost wakeup**: `unlock()` transferred ownership to a waiter,
  but the waiter's `lock()` re-entered the self-deadlock branch and never
  consumed the grant, leaving the mutex owned by a dead thread. Added
  `Thread::granted` and a check for it before the self-deadlock test.
* **Mutexes held by dead threads**: `thread::exit()` now calls
  `mutex::release_all()`, which hands off or frees every mutex owned by the
  terminating thread.
* **Semaphore lost permit**: `signal()` handed a permit to a waiter without
  incrementing the count, so the woken thread re-checked the count, saw zero and
  blocked again. `signal()` now publishes the permit before waking a waiter, and
  the new `semtest` command verifies that two consumers are released one signal
  at a time.
* **Formatter corruption**: the original `kvsnprintf` mishandled `%llu`, left
  alignment and zero padding, producing ragged thread/scheduler tables. Rewrote
  field rendering (`emit_field`) and number conversion, added flags, width,
  precision and length modifiers.
* **Permission string aliasing**: `perm_string()` returned one static buffer, so
  two masks in one `kprintf` printed the same value. It now rotates four buffers.
* **`touch` printed synthetic permissions**: it now reads back the created file
  and prints the real owner and masks.
* **Input queue overflow during tests**: raised `INPUT_QUEUE_SIZE` from 256 to
  512 bytes.
* **VFS test used the wrong error symbol**: missing-file reads now expect
  `VFS_ENOENT` and report it explicitly.
* **PIE/TEXTREL link warning**: link with `-static -no-pie`.
* **Test harness path assumptions**: `boot_test.sh` and `long_test.sh` now
  `cd` relative to their own location instead of a hard-coded absolute path.
* **`make run` needed a display**: it now defaults to `-display none` (serial
  console) and only opens a window with `GUI=1`; `make debug` starts paused with
  the GDB stub.

### Changed

* Kernel critical sections use a nestable `kernel_lock()`/`kernel_unlock()`
  instead of ad-hoc `cli`/`sti`.
* Scheduler context switching is done by returning a new interrupt frame from
  `interrupt_dispatch`, with `mov esp,eax` in `isr_common`.
* Thread creation accepts `start_ready=false` so the NEW state can be observed
  and tested; `thread::start()` performs NEW → READY.
* `make` now verifies the ELF class and Multiboot2 header on every build.
* Test logs are written to `tests/logs/` so `make clean` no longer destroys
  evidence.

### Known at release

* Ring 3 shares the kernel identity map (no memory isolation).
* Access control is logical, not hardware enforced.
* No persistence, no SMP, no user program loader, no shell history.
* Terminated thread slots are not reclaimed.
* Build still emits a few unused-function warnings.

See [KNOWN_ISSUES.md](KNOWN_ISSUES.md) for the full list.
