# SharkXOS

SharkXOS is a small monolithic operating system for **32-bit x86 (IA-32)**. It
is written in freestanding C++17 and NASM assembly, boots through **GRUB 2 /
Multiboot 2**, and runs under **QEMU**. The kernel owns the machine from the
first instruction: it installs its own GDT, IDT and interrupt controller, drives
the PIT and PS/2 keyboard, manages a kernel heap and page tables, schedules
preemptive kernel threads, exposes a RAM-disk file system with a small access
control model, and starts an interactive shell with four built-in applications.

The code is self-contained. There is no host libc, no C++ runtime, no dynamic
loader and no user-space program loader; the only allocator is the kernel heap.

| | |
|---|---|
| Target | x86 32-bit (IA-32), i686 |
| Boot | GRUB 2 with Multiboot 2 |
| Kernel | Monolithic, preemptive, single-core |
| Language | C++17 freestanding + NASM |
| Build | GNU Make, GCC/G++, NASM, xorriso, GRUB tools |
| Run | QEMU, headless serial console or VGA window |
| Version | 1.0.0 |

## Highlights

- **Boots on real firmware paths.** GRUB 2 hands control to a Multiboot 2 entry
  point; the kernel then builds its own CPU tables and interrupt infrastructure.
- **Everything is observable.** `heapinfo`, `threads`, `scheduler`, `memory`,
  `diskinfo`, `permissions` and `monitor` read live kernel structures rather
  than printing canned text.
- **Real preemptive scheduling.** Timer-driven round robin with interrupt-frame
  context switches, five thread states, sleeping and voluntary yielding.
- **Synchronisation with diagnostics.** Mutexes and counting semaphores plus
  interactive race, deadlock, lock-ordering and thread-state demos.
- **A usable userspace demo.** A ring 3 payload runs through `int 0x80`
  syscalls, and a deliberate privileged instruction faults back into the kernel
  instead of panicking the machine.
- **Four applications in the image.** Scientific calculator (line REPL and
  full-screen keypad), VFS-backed editor, file manager and live system monitor.

## Boot output

```text
SharkXOS 1.0.0 - x86 32-bit (IA-32) - monolithic kernel
[boot] multiboot2 handoff ok (magic=0x36d76289, info=0x...)
[gdt] GDT/TSS loaded: kernel CS=0x8 DS=0x10, user CS=0x1b DS=0x23
[idt] IDT loaded (256 gates), PIC remapped to 0x20-0x2f
[heap] kernel heap ready: 8192 KiB
[mmu] paging enabled: 64 MiB identity mapped, 4 KiB pages, user bit set
[vfs] ramdisk mounted: 256 KiB, 32 slots
[sched] round robin scheduler online, quantum 5 ticks (50 ms)
...
Kernel initialisation complete. 4 threads running.
Type 'help' for the command list. Built-in apps: calc, editor, files, monitor.
sharkx>
```

## Requirements

Linux, WSL or another Unix-like host with:

- GNU Make and a C++17 compiler
- NASM
- xorriso, mtools, `grub-pc-bin` and `grub-common`
- QEMU (`qemu-system-i386` from `qemu-system-x86`)
- Optional: an `i686-elf` cross compiler. The Makefile uses one when available
  and otherwise falls back to the host `gcc`/`g++` with `-m32`.

On Debian, Ubuntu or WSL:

```bash
sudo apt update
sudo apt install build-essential gcc-multilib g++-multilib nasm \
                 xorriso mtools grub-pc-bin grub-common qemu-system-x86
```

## Quick start

```bash
make            # build the kernel, verify ELF32/Multiboot2, create the ISO
make run        # boot headless in QEMU; the serial console is your terminal
make run GUI=1  # boot with a QEMU window (requires a desktop)
```

The build produces:

- `build/kernel.elf` - the 32-bit Multiboot 2 kernel
- `build/sharkxos.iso` - the bootable GRUB image

In the default headless mode the VGA text console is mirrored to COM1, and
keyboard input is accepted from the same terminal. Type commands at the
`sharkx>` prompt. Use `shutdown` or `reboot` from the shell to leave QEMU.

## Build targets

| Target | Description |
|---|---|
| `make` | Build the kernel, verify its format, and create the ISO |
| `make iso` | Build only the bootable ISO |
| `make run` | Boot headless with the serial console attached to the terminal |
| `make run GUI=1` | Boot with a QEMU window |
| `make debug` | Start QEMU paused with a GDB stub on `tcp::1234` |
| `make check` | Verify that the kernel is ELF32 and has a Multiboot 2 header |
| `make clean` | Remove `build/` |
| `make help` | Print the available targets |

For debugging:

```bash
make debug
i686-elf-gdb build/kernel.elf -ex 'target remote :1234'
```

If no cross GDB is installed, a host `gdb` that understands i386 works as well.

## Using the shell

The shell reads one line at a time (up to 127 characters, up to 8 arguments)
and matches command names exactly.

- `Ctrl-C` cancels the current line.
- `Ctrl-L` clears the screen and redraws the prompt.
- `help` prints the authoritative command list.
- `about`, `version` and `uptime` report build and runtime information.
- `diag` checks the VGA buffer, interrupt state and PIT.

A short tour:

```text
help
about
diag
memory
heapinfo
threads
scheduler
ls
touch notes.txt
write notes.txt hello from SharkXOS
cat notes.txt
calc
calc ui
editor notes.txt
files
monitor
usermode
usermode fault
```

### Command reference

Core shell:

| Command | Purpose |
|---|---|
| `help` | List every registered command |
| `clear` | Clear the screen |
| `about` | Kernel, architecture and build summary |
| `version` | Version and compiler/build timestamp |
| `uptime` | Time since boot and PIT tick count |
| `logo` | Print the SharkXOS banner |
| `echo <text>` | Echo arguments back |
| `yield` | Voluntarily give up the CPU |
| `diag` | Console, interrupt and timer diagnostics |
| `shutdown` | Power off through QEMU ACPI |
| `reboot` | Reset the machine |

Memory and scheduling:

| Command | Purpose |
|---|---|
| `memory` | Physical memory, heap and RAM-disk summary |
| `heapinfo` | Heap statistics and the first blocks in the block map |
| `threads` | Thread table with states, CPU ticks and wait targets |
| `scheduler` | Scheduler counters, quantum and mutex owners |
| `states` | Walk a thread through all five states |

Synchronisation demos:

| Command | Purpose |
|---|---|
| `race [n]` | Compare an unsynchronised and a mutex-protected counter |
| `deadlock` | Show two threads deadlocking on two mutexes |
| `lockorder` | Show ordered acquisition avoiding the deadlock |

RAM disk:

| Command | Purpose |
|---|---|
| `ls` | List files with size and owner |
| `touch <file>` | Create an empty file |
| `cat <file>` | Print a file |
| `write <file> <text>` | Append text to a file |
| `rm <file>` | Delete a file |
| `diskinfo` | RAM-disk capacity and usage |

Access control and ring 3:

| Command | Purpose |
|---|---|
| `whoami` | Show the current security principal |
| `su <system\|user\|restricted>` | Switch principal |
| `permissions <file>` | Show owner, permission bits and I/O counters for a file |
| `usermode [fault]` | Run a ring 3 payload; `fault` tries `cli` |

Applications:

| Command | Purpose |
|---|---|
| `calc` | Scientific calculator REPL |
| `calc ui` | Full-screen arrow-key calculator |
| `editor [file]` | VFS-backed line editor |
| `files` | File manager |
| `monitor` | Live system monitor |

## Applications

### Calculator

`calc` is a recursive-descent expression evaluator with `+ - * / % ^ !`, unary
minus, parentheses and implicit multiplication (`2pi`, `3(4+1)`). It supports
`sin cos tan asin acos atan ln log sqrt exp abs floor ceil round fact`, the
constants `pi` and `e`, `ncr(n,r)` and `npr(n,r)`, degree/radian mode,
binary/decimal/hex conversion, memory (`m+ mr mc`), `ans` and history.

`calc ui` draws a 7x5 keypad directly into VGA text memory. Arrow keys move the
highlighted key, Enter presses it, and typed expressions can also be evaluated
directly. The screen is mirrored to the serial console with ANSI cursor
addressing, so the UI works in both windowed and headless sessions.

### Editor

`editor` is a line-oriented editor backed by the kernel VFS. Bare text appends a
line; commands include `:l :p :i :e :d :n :w :wq :q :q! :s :h`. It loads and
saves through the VFS, warns about unsaved changes, and allocates its line and
I/O buffers from the kernel heap while it is running.

### File manager

`files` provides `ls cat touch write rm ren info df`, with owner metadata in the
listing and permission bits in the `info` view.

### System monitor

`monitor` renders live kernel data only: uptime, paging state, scheduler
counters, thread table, heap statistics, RAM-disk usage and the current
principal. It refreshes from the actual kernel structures on every frame.

## Architecture

```text
GRUB 2
  -> Multiboot 2 handoff
    -> boot/entry.asm : _start
      -> kmain (kernel/main.cpp)
         console + Multiboot 2 parsing
         GDT/TSS + IDT/PIC
         heap + paging
         access control + RAM-disk VFS
         mutexes + keyboard + scheduler
         PIT + IRQs + serial
         heartbeat/memwatch threads
         logo + shell loop
```

The boot thread is adopted as the first kernel thread. Two background threads
(`heartbeat` and `memwatch`) run alongside the shell, and the scheduler keeps an
idle thread available when nothing else is runnable.

### Subsystems

| Subsystem | Implementation |
|---|---|
| Boot | GRUB 2 / Multiboot 2, 32-bit protected mode, kernel linked at 1 MiB |
| CPU tables | GDT with kernel/user code and data segments, TSS for ring 3 stack switching, 256 IDT gates |
| Interrupts | PIC remapped to `0x20-0x2f`, exception handlers, IRQ dispatch, `int 0x80` syscall gate |
| Timer | PIT at 100 Hz; uptime, sleep and scheduler quantum |
| Heap | 8 MiB boundary-tagged allocator in BSS; 16-byte alignment, magic/checksum headers, immediate coalescing, OOM and double/invalid-free detection |
| Paging | 32-bit two-level paging, 4 KiB pages, first 64 MiB identity mapped with the user bit set |
| Threads | Up to 24 kernel threads, 16 KiB default stacks, NEW/READY/RUNNING/WAITING/TERMINATED, sleep/yield/exit |
| Scheduler | Preemptive round robin, 5-tick (50 ms) quantum, interrupt-frame context switching |
| Synchronisation | Mutexes with ownership, blocking and recursive mode; ordered locking; counting semaphores |
| VFS | RAM disk with 32 slots, 256 KiB total, 64 KiB per file, 31-character names, create/read/write/truncate/rename/delete and stats |
| Security | Logical principals SYSTEM/USER/RESTRICTED with per-file r/w/d bits; SYSTEM bypass and owner rights |
| Ring 3 | Identity-mapped user pages, TSS stack switch, `int 0x80` syscalls, ring 3 fault recovery |
| Drivers | VGA text console (80x25), PS/2 keyboard with arrows and modifiers, 16550 UART on COM1 |
| Shell | 128-byte line buffer, 8 arguments, command registry, line editing with Ctrl-C/Ctrl-L |
| Apps | Calculator, editor, file manager and monitor built into the kernel image |

### System calls

| Number | Name | Behaviour |
|---|---|---|
| 1 | `SYS_WRITE` | Write a buffer to the console (capped at 1024 bytes) |
| 2 | `SYS_EXIT` | Return from ring 3 to the kernel exit stub |
| 3 | `SYS_PUTC` | Write one character |
| 4 | `SYS_TICKS` | Return the current PIT tick count |
| 5 | `SYS_WHOAMI` | Return the current security principal |

## Memory and limits

| Resource | Value |
|---|---|
| Kernel load address | 1 MiB |
| Identity-mapped address space | 64 MiB |
| Kernel heap | 8 MiB |
| Maximum threads | 24 |
| Default thread stack | 16 KiB |
| PIT frequency | 100 Hz |
| Scheduler quantum | 5 ticks / 50 ms |
| RAM disk | 256 KiB, 32 files, 64 KiB per file |
| Maximum filename | 31 characters (32-byte field) |
| Shell line / arguments | 127 characters (128-byte buffer) / 8 arguments |
| Mutexes / waiters per mutex | 16 / 8 |
| QEMU memory in `make run` | 128 MiB |

## Repository layout

```text
boot/        Multiboot 2 entry, GDT/TSS, IDT stubs, ISR common path
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
grub/        GRUB configuration
tools/       build, toolchain and QEMU helper scripts
build/       generated objects, kernel and ISO (ignored by git)
```

## Extending SharkXOS

A shell command is a function with the `ShellCommand` signature plus one entry
in the command table:

```cpp
static int cmd_hello(int argc, char** argv) {
    kprintf("hello from SharkXOS\n");
    return 0;
}

// in CORE_COMMANDS[] or SYSTEM_COMMANDS[]
{"hello", "say hello", cmd_hello},
```

Applications follow the same pattern through `apps::*_main(int, char**)` in
`include/apps/apps.h`. New system calls are added in `kernel/syscall.cpp`, with
the corresponding user stub issuing `int $0x80`.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `grub-mkrescue: command not found` | Install `grub-pc-bin`, `grub-common`, `mtools` and `xorriso` |
| 32-bit compile or `-lgcc` errors | Install `gcc-multilib` and `g++-multilib`, or use an `i686-elf` cross compiler |
| `qemu-system-i386: command not found` | Install `qemu-system-x86`, or pass `QEMU=/path/to/qemu-system-i386` |
| No window appears | Headless mode is the default; use `make run GUI=1` on a machine with a desktop |
| Cross compiler not found | The build falls back to host `gcc -m32`; install an `i686-elf` toolchain for the canonical build |
| QEMU keeps running | Run `shutdown` or `reboot` in the shell, or stop QEMU from the host |

## Limitations

This is a teaching kernel, not a secure multi-user system. Paging identity-maps
64 MiB with the user bit set, so ring 3 code can read kernel memory; the access
control model is a logical principal switch inside ring 0, not hardware
isolation. The scheduler is a simple round robin with a fixed quantum. There is
no disk driver, no networking, no SMP, no FPU context switching, no wall-clock
RTC and no user-space program loader. The RAM disk is volatile, and the whole
system is single-core and 32-bit by design.
