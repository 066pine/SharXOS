# SharkXOS

SharkXOS is a small monolithic operating system for **32-bit x86 (IA-32)**. It is
written in freestanding C++17 and NASM assembly, boots through **GRUB 2 /
Multiboot 2**, and runs under **QEMU**. The kernel brings up its own GDT, IDT,
interrupt controller, PIT, paging, heap, scheduler, RAM-disk file system and
security model, then starts an interactive shell with four built-in
applications.

No host libc, no C++ runtime, no dynamic allocation outside the kernel heap.

```
SharkXOS 1.0.0 - x86 32-bit (IA-32) - monolithic kernel
[ boot ] multiboot2 handoff ok (magic=0x36d76289, info=0x...)
...
Kernel initialisation complete. 4 threads running.
Type 'help' for the command list. Built-in apps: calc, editor, files, monitor.
sharkx>
```

## Quick start

Prerequisites (Ubuntu / WSL): `build-essential nasm xorriso mtools grub-pc-bin
grub-common qemu-system-x86` and, ideally, an `i686-elf` cross compiler.

```bash
make            # build kernel, verify ELF32/Multiboot2, create the ISO
make run        # boot headless in QEMU (serial console on the terminal)
make run GUI=1  # boot with a QEMU window (needs a desktop)
make test       # boot and run the smoke suite
make clean
```

The bootable image is `build/sharkxos.iso` (also `build/kernel.elf`). Full
instructions, including how to install the cross compiler, are in
[BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md).

## What is implemented

| Area | Status |
|---|---|
| Multiboot 2 boot via GRUB 2, 32-bit protected mode | working |
| GDT, TSS, IDT, PIC remap, exception handlers | working |
| PIT at 100 Hz, uptime, sleep | working |
| Kernel heap: `kmalloc/kfree/kzalloc/krealloc`, stats, OOM, double/invalid-free detection, coalescing | working, 8/8 self-test |
| Kernel threads with NEW/READY/RUNNING/WAITING/TERMINATED | working, `states` demo |
| Preemptive round-robin scheduler, real interrupt-frame context switching | working |
| Mutex + counting semaphore, race demo, deadlock demo, cycle detection, lock ordering | working |
| RAM-disk VFS (32 files, 256 KiB, 64 KiB max per file), create/read/write/truncate/rename/delete, ENOSPC | working |
| Logical access control (SYSTEM / USER / RESTRICTED) with r/w/d bits | working (logical, not hardware enforced) |
| Ring 3 demo with identity-mapped user pages, TSS stack switch, `int 0x80` syscalls, fault recovery | working (coarse isolation) |
| CLI shell with 37 registered commands | working |
| Scientific calculator | working |
| Text editor | working |
| File manager | working |
| System monitor | working |

## Shell commands

Built-ins: `help clear about version uptime logo echo yield diag shutdown reboot`

Applications: `calc editor files monitor`

Kernel subsystem commands:

```
memory heapinfo memtest            heap statistics and allocator self-test
threads scheduler states           threads, scheduler stats, thread-state demo
race deadlock lockorder semtest    synchronisation demos and test
ls touch cat write rm diskinfo     RAM-disk files
vfstest                            fill the RAM disk and verify ENOSPC
whoami su permissions access_test  access control
usermode [fault]                   ring 3 payload / fault recovery
```

Every registered command performs real work against kernel state; there are no
placeholder commands that print canned text.

## Applications

* **calc** – recursive-descent expression parser with `+ - * / % ^ !`, unary
  minus, parentheses, `sin cos tan asin acos atan ln log sqrt exp abs floor ceil
  round fact`, constants `pi e`, `ncr(n,r)`, `npr(n,r)`, degree/radian mode,
  binary/decimal/hex conversion, memory (`m+ mr mc`), `ans`, and history.
* **editor** – VFS-backed line editor (`:l :i :e :d :a :w :wq :q :q! :s :h`),
  load/save through the kernel VFS, warns on unsaved changes, and keeps its line
  and file buffers on the kernel heap while it runs.
* **files** – file manager (`ls cat touch write rm ren info df`).
* **monitor** – system monitor rendering live kernel data only (uptime,
  scheduler counters, thread table, heap stats, RAM-disk usage, principal).

## Repository layout

```
boot/        Multiboot2 entry, GDT/TSS, IDT stubs, ISR common path
kernel/      main, shell, commands, GDT/IDT, interrupts, PIT, paging,
             syscalls, ring 3 loader, panic, kernel lock
drivers/     VGA text console, PS/2 keyboard, serial (COM1)
memory/      boundary-tagged coalescing heap
process/     threads and preemptive round-robin scheduler
sync/        mutex and counting semaphore
fs/          RAM-disk VFS with permissions
security/    logical principals and access checks
lib/         freestanding string/memory helpers and math library
apps/        calculator, editor, file manager, monitor, shared TUI helpers
include/     all headers
tests/       serial command scripts + captured logs (tests/logs/)
tools/       headless QEMU test harnesses
```

## Documentation

| Document | Purpose |
|---|---|
| [PROJECT_AUDIT.md](PROJECT_AUDIT.md) | requirement-by-requirement audit |
| [ARCHITECTURE.md](ARCHITECTURE.md) | design and internals |
| [DEVELOPMENT_PLAN.md](DEVELOPMENT_PLAN.md) | phases, milestones and deviations |
| [BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md) | toolchain and build/run guide |
| [TEST_PLAN.md](TEST_PLAN.md) | test strategy and cases |
| [TEST_RESULTS.md](TEST_RESULTS.md) | captured results from real runs |
| [KNOWN_ISSUES.md](KNOWN_ISSUES.md) | limitations and open bugs |
| [CHANGELOG.md](CHANGELOG.md) | change history |
| [REPORT.md](REPORT.md) | academic report (chapters I–IV) |

## Limitations in one paragraph

This is a teaching kernel, not a secure multi-user system. Paging identity-maps
64 MiB with the user bit set, so ring 3 code can read kernel memory; the access
control model is a logical principal switch inside ring 0, not hardware
isolation; the scheduler is a simple round robin with a fixed quantum; there is
no disk driver, no networking, no SMP and no user-space program loader. Details
and more items are in [KNOWN_ISSUES.md](KNOWN_ISSUES.md).
