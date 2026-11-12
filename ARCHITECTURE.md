# SharkXOS – Architecture

This document describes how SharkXOS is put together: the boot path, memory
layout, interrupt/context-switch mechanism, and each kernel subsystem. File
paths refer to the source tree.

---

## 1. System overview

SharkXOS is a **monolithic 32-bit kernel**. Everything runs in ring 0 in a
single address space, except for an optional ring 3 demo. A preemptive
round-robin scheduler multiplexes kernel threads; the shell is simply one of
those threads.

```
GRUB 2 (Multiboot 2)
        │  magic + info pointer
        ▼
boot/entry.asm  ──►  kmain()  ──►  subsystem init
                                    │
      ┌─────────────────────────────┼──────────────────────────────┐
      ▼             ▼               ▼              ▼               ▼
   GDT/TSS        IDT/PIC          heap          paging          VFS
      │             │               │              │               │
      └─────────────┴───────┬───────┴──────────────┴───────────────┘
                            ▼
                   scheduler + threads
                            │
                            ▼
                    shell (interactive)  ──►  calc / editor / files / monitor
```

Kernel source size: about 6 500 lines of C++/assembly across 32 translation
units, plus headers.

## 2. Boot sequence

`kmain(magic, info_addr)` in `kernel/main.cpp` performs initialisation in this
order (each step logs a `[ tag ]` line):

1. **VGA console** – clear screen, print banner (`console::init`).
2. **Multiboot 2 parse** – validate magic `0x36d76289`, copy the memory map,
   bootloader name and available RAM into `BootInfo`.
3. **GDT + TSS** – kernel CS `0x08`, kernel DS `0x10`, user CS `0x1b`, user DS
   `0x23`; TSS holds `esp0` for ring 3 → ring 0 transitions.
4. **IDT** – 256 gates; exceptions 0–31, IRQs remapped to `0x20–0x2f`, syscall
   gate `0x80`, yield gate `0x81`.
5. **Heap** – initialise the 8 MiB BSS region.
6. **Paging** – build a 64 MiB identity map (4 KiB pages, P|RW|US) and enable
   CR0.PG. Needed for ring 3.
7. **Access control** – reset principals and counters.
8. **VFS** – mount the 256 KiB RAM disk with 32 slots.
9. **Mutexes**, **keyboard**, **scheduler** – create the idle thread and adopt
   the boot context as thread `shell`.
10. **PIT** at 100 Hz, enable timer/keyboard/serial IRQs.
11. **Heartbeat** and **memwatch** kernel threads.
12. Print the logo, mark the kernel ready, enter `shell::loop()`.

The boot context is not thrown away: `thread::adopt_boot_thread("shell")`
registers the current stack as a normal `Thread`, so the shell participates in
scheduling and can be preempted like any other thread.

## 3. Memory layout

| Region | Address / size | Notes |
|---|---|---|
| Kernel image | loaded at 1 MiB by GRUB | `.text`, `.rodata`, `.data`, `.bss` |
| Kernel heap | 8 MiB in `.bss`, 16-byte aligned | one static arena, no physical allocator |
| Paging structures | 4 KiB page directory + 16 page tables | in `.bss`, 64 MiB identity mapped |
| RAM disk | 256 KiB static buffer | file payloads are heap allocations |
| Thread stacks | 8–16 KiB each, from `kmalloc` | freed by the scheduler reaper |
| User demo payload | compiled into `.rodata` | executed at CPL=3 |

There is **no physical frame allocator** and no virtual memory beyond the
identity map. This is a deliberate simplification: the kernel never needs to
grow beyond the memory GRUB gives it, and identity mapping keeps pointer values
valid for the page tables, TSS and user payload.

## 4. Interrupts and context switching

All ISR stubs are generated in `boot/interrupts.asm`. Each pushes a dummy error
code when the CPU does not, then jumps to `isr_common`, which:

1. pushes the general-purpose registers (the layout matches `struct Registers`);
2. loads the kernel data selector;
3. calls `interrupt_dispatch(Registers*)`;
4. stores the returned pointer in `eax` and executes `mov esp, eax`;
5. pops registers, `add esp, 8` (vector + error code) and `iret`.

Because the dispatcher may return a **different** interrupt frame, step 4 is a
real context switch: the new `esp` points at another thread's saved frame, and
`iret` resumes that thread. No separate assembly switch routine is needed.

`interrupt_dispatch` (`kernel/interrupts.cpp`) routes:

| Vector | Action |
|---|---|
| 0–31 | print diagnostic, panic for fatal exceptions; ring 3 faults return to the shell via `user_mode_abort` |
| `0x20` (IRQ0) | `scheduler::tick(r)` → maybe preempt |
| `0x21` (IRQ1) | drain PS/2 keyboard |
| `0x24` (IRQ4) | drain COM1, push bytes into the keyboard queue |
| `0x80` | syscall handler |
| `0x81` | `scheduler::schedule(r)` (voluntary yield) |

The timer handler increments tick counters, wakes sleeping threads, reaps
terminated stacks, and decrements the quantum. When the quantum reaches zero it
calls `schedule()`, which saves the current frame and returns the next thread's
frame. That is preemption.

## 5. Threads and scheduler

### Thread object

`struct Thread` (`include/process/thread.h`) contains id, name, state, saved
`esp`, stack bounds, entry function/argument, tick counters, wait/granted mutex
pointers and an `in_ready` flag. There are 24 static slots.

States follow the required lifecycle:

```
create(start_ready=false) ──► NEW ──start()──► READY ──schedule()──► RUNNING
                                                ▲                      │
                                                │   sleep / mutex      │
                                                └──── WAITING ◄────────┘
                                                       │
                                             exit() ──► TERMINATED
```

`thread::create` also allocates the stack and fabricates an initial interrupt
frame at its top: `eip = thread_entry_stub`, `cs = KERNEL_CS`,
`eflags = 0x202`, `esp = top`. The stub enables interrupts, calls the entry
function and then `thread::exit(0)`.

### Scheduler

`process/scheduler.cpp` implements a circular ready queue of up to 24 entries.

* **Quantum**: 5 ticks = 50 ms at 100 Hz.
* **Preemption**: on quantum expiry, the running thread is pushed back as READY
  and the next READY thread is selected.
* **Voluntary yield**: `int 0x81`, used by `thread::yield`, `sleep_ms` and
  blocking mutex operations.
* **Idle thread**: if nothing is READY, the idle thread runs `hlt` until the next
  interrupt (counted in `kernel::idle_halts`).
* **Reaping**: on each tick, stacks of TERMINATED threads (except the current
  one) are freed once; the slot itself stays until reused.

The scheduler never picks TERMINATED or WAITING threads. Statistics
(`ticks`, `context_switches`, `preemptions`, `ready_count`, running id/name) are
exposed through `scheduler::stats()` and shown by `scheduler` and `monitor`.

### Time

The PIT is programmed to 100 Hz. `time::ticks()` is the monotonic tick counter;
`time::uptime_ms()` derives milliseconds. `thread::sleep_ms` computes an absolute
wake tick and moves the thread to WAITING; the timer handler wakes it.

## 6. Synchronisation

### Kernel lock

`kernel_lock()`/`kernel_unlock()` (`kernel/lock.cpp`) maintain a nesting depth and
execute `cli` on the outermost lock / `sti` on the outermost unlock. All heap,
VFS, scheduler and mutex metadata operations are protected this way. It is a
coarse "interrupts off" lock, adequate for a single CPU.

### Mutex

`sync/mutex.cpp` implements blocking mutexes with:

* an owner pointer, recursion depth, and optional recursive mode;
* a FIFO waiter queue (8 slots);
* **direct handoff**: `unlock()` transfers ownership to the first waiter, sets
  `next->granted = m`, marks it READY and enqueues it. The waiter's `lock()`
  consumes the `granted` flag instead of blocking again, which closes the
  classic lost-wakeup window;
* **self-deadlock rejection** when a non-recursive mutex is re-locked by its
  owner;
* **wait-for cycle detection**: before blocking, `would_deadlock()` walks
  `m->owner` → `owner->wait_mutex->owner` … and aborts if it reaches the
  requester;
* **release on death**: `thread::exit()` calls `mutex::release_all(t)`, handing
  each owned mutex to the next waiter (or freeing it), so a dying demo thread
  can never strand a lock.

### Semaphore

`struct Semaphore` is a counting semaphore with an initial count and a waiter
queue. `wait()` decrements a positive count and otherwise blocks; `signal()`
increments the count *first*, then wakes one waiter. Publishing the permit before
the wake is important: the woken thread re-checks the count, so the permit is
never lost if another thread consumes it first. `semtest` starts two consumers
at count 0, verifies that both are WAITING, releases them one signal at a time
and checks that the final count is 0.

### Demonstrations

* `race [n]`: two threads increment a counter with a forced `yield()` between
  load and store. Run 1 without a mutex (lost updates are expected and counted),
  run 2 with a mutex (exact total). Verdict is computed, not printed blindly.
* `deadlock`: thread A takes `m1` then `m2`, thread B takes `m2` then `m1`. The
  cycle detector aborts one acquisition, reports the cycle and lets both threads
  terminate; the shell stays responsive.
* `lockorder`: both threads acquire the mutexes in address order through
  `lock_ordered()`, demonstrating prevention. The counter must reach 2 × N.

## 7. Kernel heap

`memory/heap.cpp` manages an 8 MiB static arena with a doubly-linked,
address-ordered list of blocks. Each block has a 32-byte header:

```
magic | size | prev_size | free | prev | next | checksum | reserved
```

* **Alignment**: 16 bytes; payload follows the header.
* **Split**: an allocation splits a free block if the remainder can hold a header
  plus 16 bytes.
* **Coalesce**: `kfree` merges with the next and previous physical neighbours
  immediately, so no two adjacent blocks are ever free.
* **Validation**: `kfree` rejects pointers outside the arena and headers with a
  bad magic/checksum; a freed block is stamped `HEAP_MAGIC_DEAD`, so a second
  free is detected and counted instead of corrupting the list.
* **OOM**: an oversized request or a failed search returns `nullptr` and bumps
  `fail_count`.
* **Stats**: used/free/peak, block counts, largest free block, alloc/free counts,
  OOM/double-free/invalid-free counters, coalesce count.
* **Tests**: `heap::self_test()` returns the number of failed checks. It covers
  alignment, payload writability, double free, invalid free, coalescing, OOM,
  `krealloc` data preservation and a stress loop; `memtest` reports 8/8 PASS.

## 8. File system and access control

### RAM-disk VFS

`fs/vfs.cpp` exports:

```
create remove read write truncate rename find at count stats check_access
```

* 32 files, names up to 31 chars, 256 KiB total capacity, 64 KiB maximum per
  file.
* File payloads are heap buffers grown on demand (or allocated in one go for a
  single write). `VFS_ENOSPC` is returned when the payload cannot fit.
* Metadata per file: owner principal, user permissions, restricted permissions,
  created/modified ticks, read/write counters.
* All operations take the kernel lock and check permissions through
  `security/access.cpp`.

### Access model

Three logical principals: `SYSTEM`, `USER`, `RESTRICTED`. Each file stores a
3-bit mask (read/write/delete) for `USER` and for `RESTRICTED`; `SYSTEM` has full
access. `su <principal>` switches the current principal, `whoami` prints it,
`permissions <file>` shows the masks, and `access_test` exercises the allow/deny
matrix.

This is **logical** enforcement inside ring 0. It is not a hardware security
boundary, and the documentation says so. The separate ring 3 demo shows the CPU
privilege transition but, due to the coarse identity map, does not isolate
memory either.

### Ring 3 demo

`kernel/usermode.cpp`:

1. Saves the current interrupt frame on the kernel stack.
2. Sets the TSS `esp0` so the CPU has a kernel stack for interrupts from ring 3.
3. Builds a frame with user CS (`0x1b`), user DS (`0x23`), `eflags` with IF set,
   and `iret`s into a compiled-in payload.
4. The payload prints via `int 0x80` syscall 1 (`write`), reads the tick counter
   via syscall 4, and exits via syscall 2.
5. Syscalls are dispatched in `kernel/syscall.cpp`; `write`/`putc` go to the
   console, `ticks` reads the scheduler tick count, `whoami` returns the
   principal, `exit` restores the saved kernel frame.
6. If the payload executes a privileged instruction (`cli`), the CPU raises
   #GP (vector 13). `user_mode_abort()` rewrites the frame to return to ring 0
   and reports the fault; the shell continues.

## 9. Console, keyboard and serial

* **VGA text mode** at `0xB8000`, 80×25, with colour attributes and a small
  scrollback-free console (`drivers/vga.cpp`, `drivers/console.cpp`).
* `kprintf` implements `%d %i %u %x %X %p %c %s %f`, flags `- 0 +`, width,
  precision and `l`/`ll`/`z` length modifiers, with no libc.
* **Keyboard**: PS/2 scancode set 1, modifier tracking, a 512-byte ring buffer;
  IRQ1 fills it.
* **Serial**: COM1 at 115200 8N1. Every console character is mirrored to the
  serial port, and received bytes are injected into the keyboard queue. This is
  what makes fully headless automated testing possible: QEMU's `-serial stdio`
  becomes both the display and the keyboard.

## 10. Shell and applications

`kernel/shell.cpp` owns the command table, whitespace tokeniser, prompt
(`sharkx>`), line editing (backspace, Ctrl-C, Ctrl-L) and shutdown/reboot.
Commands are registered by `register_system_commands()`.

Applications are normal kernel modules with their own REPL loops:

* **Calculator** – tokeniser + recursive-descent parser (expression → term →
  power → unary → primary), function table, history ring, memory register,
  angle mode, base conversion.
* **Editor** – 64×96 line buffer and 8 KiB file scratch buffer allocated from
  the kernel heap at entry and freed at exit, loads/saves through the VFS,
  colon commands, dirty flag and unsaved-changes warning.
* **File manager** – thin command layer over the VFS.
* **Monitor** – samples `time`, `scheduler`, `thread`, `heap`, `vfs` and
  `access` APIs once per second and redraws; any key returns to the shell.

The monitor and other apps only read live kernel structures; no value is
hard-coded.

## 11. Design trade-offs

| Decision | Why | Cost |
|---|---|---|
| Identity mapping instead of per-process page tables | Simple, enough for ring 3 transition | no memory isolation |
| Static 8 MiB heap in BSS | No physical allocator needed | fixed size, no growth |
| Interrupt-frame switching | One mechanism for IRQ, yield and syscall | every switch goes through the ISR path |
| Coarse `cli`/`sti` kernel lock | Easy to reason about on one CPU | no SMP, longer interrupt latency |
| Logical principals | Demonstrates access checks without a userland | not a security boundary |
| RAM disk | No disk driver required | data lost on reset |
| Serial mirroring | Headless CI-style testing | small overhead per character |

## 12. Source map

| Path | Responsibility |
|---|---|
| `boot/entry.asm` | Multiboot2 header, stack setup, call `kmain` |
| `boot/descriptors.asm` | GDT load, TSS load, segment reload |
| `boot/interrupts.asm` | ISR/IRQ stubs and the common switch path |
| `kernel/main.cpp` | boot sequence, heartbeat, memwatch |
| `kernel/shell.cpp` | prompt, parser, core commands, shutdown |
| `kernel/commands.cpp` | subsystem commands and demos |
| `kernel/interrupts.cpp` | vector dispatch |
| `kernel/time.cpp` | PIT and sleep |
| `kernel/lock.cpp` | nestable interrupt lock |
| `kernel/paging.cpp` | identity page tables |
| `kernel/syscall.cpp`, `kernel/usermode.cpp` | ring 3 and `int 0x80` |
| `memory/heap.cpp` | allocator |
| `process/thread.cpp`, `process/scheduler.cpp` | threading |
| `sync/mutex.cpp` | mutexes, deadlock detection, semaphores |
| `fs/vfs.cpp` | RAM-disk file system |
| `security/access.cpp` | principals and permission checks |
| `drivers/*` | VGA, console/printf, keyboard, serial |
| `lib/string.cpp`, `lib/kmath.cpp` | freestanding helpers and math |
| `apps/*` | the four applications |
