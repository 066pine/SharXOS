# SharkXOS: A 32-bit Monolithic Operating System

**Course project report**

| | |
|---|---|
| System | SharkXOS 1.0.0 |
| Target | IA-32 (x86 32-bit protected mode), Multiboot 2 |
| Toolchain | NASM 2.16.01, i686-elf GCC 15.2.0, binutils 2.45 |
| Emulator | QEMU 8.2.2 (`qemu-system-i386`, 128 MiB RAM) |
| Date | 12 November 2026 |

---

## Chapter I – Introduction

### 1.1 Motivation

Operating-system courses usually present processes, scheduling, virtual memory
and file systems as separate abstractions. Building a small kernel makes the
connections between them concrete: a scheduler is only useful if there is a
stack to switch to, a mutex only works if the scheduler can block and wake
threads, and a file system only exists if the heap can hand out buffers. The
goal of this project was to build such a system from an empty directory and to
verify every required behaviour on the real kernel rather than on paper.

SharkXOS is a monolithic 32-bit kernel written in freestanding C++17 and NASM.
It boots through GRUB 2 using Multiboot 2, brings up its own descriptor tables,
interrupts, timer, paging, heap, scheduler, synchronisation primitives, RAM-disk
file system and security model, and then starts an interactive shell with four
applications. There is no host libc, no host allocator and no long mode.

### 1.2 Objectives

The concrete objectives, taken from the assignment, were:

1. A Multiboot 2 boot path and a CLI shell with a `sharkx>` prompt and at least
   three working custom commands.
2. Kernel threads with the five canonical states (NEW, READY, RUNNING, WAITING,
   TERMINATED), at least two kernel threads, and preemptive round-robin
   scheduling with a real context switch.
3. A mutex and a semaphore, a race-condition demonstration, a deadlock
   demonstration and a prevention mechanism.
4. A kernel heap with `kmalloc`/`kfree`, statistics, out-of-memory handling and
   detection of double and invalid frees.
5. A RAMDisk VFS with CRUD operations and `ls`, `touch`, `cat`, `write`, `rm`,
   `diskinfo`.
6. Access control with `whoami`, `permissions`, `access_test`, or an honest
   statement about the absence of ring 3.
7. Four applications: scientific calculator, text editor, file manager and
   system monitor.
8. A build system with `make`, `make clean`, `make iso`, `make run` and
   `make debug`.
9. A complete documentation set and a final report.
10. Strict IA-32, no host libc, and no fabricated output: every command must do
    real work.

A meta-objective, stated explicitly in the assignment, was honesty. Where a
feature could not be completed properly, the report was to say so rather than
fake a screenshot or a canned transcript. That constraint shaped several design
decisions and is reflected in Chapter IV.

### 1.3 Scope and non-goals

The project targets a single CPU, BIOS/legacy boot, a text console and a
volatile RAM disk. It does not attempt SMP, networking, USB, persistent storage,
a POSIX layer, a graphical desktop or a secure multi-user environment. Those
exclusions are deliberate: each would require a large subsystem (APIC, block
driver, network stack, windowing) that would dilute the core operating-system
concepts the project is meant to demonstrate.

### 1.4 Report structure

Chapter II presents the architecture and the reasoning behind the main design
choices. Chapter III describes the implementation, including the bugs found
during bring-up, and the test methodology. Chapter IV reports measured results,
discusses limitations, lists lessons learned and concludes. The AI-usage
disclosure and references follow Chapter IV.

---

## Chapter II – Design

### 2.1 Platform and toolchain

The kernel is compiled with an `i686-elf` cross compiler (GCC 15.2.0) using
`-march=i686 -ffreestanding -fno-exceptions -fno-rtti -fno-builtin
-fno-stack-protector -fno-pic -fno-pie -mno-sse -mno-mmx`. Assembly is NASM
`elf32`. Linking uses a custom script with `-nostdlib -static -no-pie`, which
places the kernel at 1 MiB and keeps the Multiboot 2 header in the first 32 KiB
of the file.

A host `g++ -m32` fallback exists, but the cross compiler is the supported path:
it cannot accidentally pick up host headers and it guarantees IA-32 code
generation. The build verifies the result on every invocation: `file` must
report `ELF 32-bit ... Intel 80386`, `readelf` must report `ELF32`, and
`grub-file --is-x86-multiboot2` must accept the header.

### 2.2 Architecture overview

SharkXOS is monolithic. All kernel subsystems share one address space and run
in ring 0. The shell is a kernel thread; applications are kernel modules called
from the shell. The only ring 3 code is an explicit demo payload.

```
+---------------------------------------------------------------+
|                         Applications                          |
|   calc        editor        files        monitor              |
+---------------------------------------------------------------+
|                      Shell / command layer                    |
+---------------------------------------------------------------+
| threads | scheduler | mutex/sem | VFS | access | syscalls     |
+---------------------------------------------------------------+
| heap | paging | GDT/TSS | IDT/PIC | PIT | keyboard | serial    |
+---------------------------------------------------------------+
|                  Multiboot 2 / GRUB 2 boot                    |
+---------------------------------------------------------------+
```

The boot order in `kmain` is: console → Multiboot parse → GDT/TSS → IDT →
heap → paging → access → VFS → mutex → keyboard → scheduler → PIT and IRQs →
boot threads → shell. The heap is initialised before paging because it lives in
BSS and is covered by the identity map created later. The scheduler is started
before the PIT so that the boot context can be adopted as a thread; timer
interrupts are enabled immediately afterwards.

### 2.3 Memory design

There is no physical frame allocator. The kernel heap is a **static 8 MiB arena
in BSS**, and paging identity-maps the first 64 MiB (4 KiB pages, present,
writable, user). This has two consequences:

* allocator code is simple and deterministic: one contiguous region, no page
  faults to handle during allocation;
* ring 3 can fetch its code and use a stack without a per-process address space,
  but kernel memory is also user-accessible.

The second consequence is the project's largest limitation and is documented as
such. A "proper" design would add a frame allocator and per-process page
directories; that was judged out of scope relative to the required feature set,
because the assignment explicitly permits an honest statement about ring 3.

Thread stacks are 8–16 KiB blocks allocated from the heap. Each new thread gets a
fabricated interrupt frame at the top of its stack, so the scheduler can start it
with the same `iret` path used for preemption.

### 2.4 Thread and scheduler design

A thread is a fixed-size record with an id, name, state, saved stack pointer,
stack bounds, entry point, argument, tick counters and synchronisation links.
The five states form the required lifecycle:

```
NEW --start()--> READY --dispatch--> RUNNING --exit()--> TERMINATED
                    ^                    |
                    |                    | sleep / lock
                    +------ WAITING <----+
```

New threads are normally created directly in READY. The optional
`start_ready=false` argument leaves a thread in NEW so that the state can be
observed and tested; `thread::start()` performs the NEW → READY transition.

The scheduler is a circular ready queue with a 5-tick quantum (50 ms at 100 Hz).
On every timer tick it increments counters, wakes expired sleepers, reaps
terminated stacks and decrements the quantum. At zero it preempts: the current
thread becomes READY and the next READY thread is selected. A voluntary yield
uses `int 0x81` and enters the same selection code.

The key design decision is that **context switching is done entirely through the
interrupt frame**. `interrupt_dispatch(Registers*)` returns a pointer to the
frame that should be resumed; the assembly stub loads it into `esp`, pops the
registers and executes `iret`. Timer preemption, yield and syscalls therefore
share one switch mechanism, which removes a whole class of stack-layout bugs
that separate switch routines tend to introduce.

### 2.5 Synchronisation design

The mutex has an owner, a recursion depth, an optional recursive mode and a FIFO
waiter queue. Two details matter:

* **Direct handoff.** `unlock()` does not merely wake a waiter; it transfers
  ownership and sets a `granted` flag on the waiter. The waiter's `lock()`
  consumes the flag and returns immediately. This avoids a window in which the
  mutex is free but the woken thread has not yet run, during which a third
  thread could steal it and break FIFO fairness.
* **Cycle detection.** Before blocking, the kernel walks the wait-for chain
  (`mutex->owner -> owner->wait_mutex -> ...`) and refuses an acquisition that
  would close a cycle. This is used by the `deadlock` demo to recover instead of
  hanging the machine.

`thread::exit()` releases every mutex still owned by the dying thread. Without
this, a terminated demo thread could leave a mutex permanently owned, and all
future waiters would block forever. A counting semaphore reuses the same waiter
queue and wake mechanism.

The kernel lock is a nestable `cli`/`sti` depth counter. It is coarse but easy to
reason about on a uniprocessor, and it protects the heap, VFS and scheduler
metadata.

### 2.6 File system and security design

The VFS is a RAM disk with 32 slots, 256 KiB total capacity and 64 KiB per file.
Each file stores a name, heap-backed payload, owner principal, two permission
masks (USER and RESTRICTED, each read/write/delete), timestamps in ticks and
read/write counters. Operations return explicit errors: `VFS_ENOENT`,
`VFS_EEXIST`, `VFS_EACCES`, `VFS_ENOSPC`, `VFS_EINVAL`, `VFS_ENAMETOOLONG`,
`VFS_EIO`, `VFS_EBUSY`.

Security is a logical principal model: `SYSTEM` has full access, `USER` and
`RESTRICTED` are checked against the per-file masks. `su` switches the current
principal; `access_test` exercises the allow/deny matrix. This is real
permission-checking code, but it is not a hardware boundary. The separate ring 3
demo shows a genuine CPL=3 transition, TSS stack switch, `int 0x80` syscalls and
fault recovery, while the documentation states clearly that the identity map
does not isolate kernel memory.

### 2.7 User interface and applications

The shell provides a `sharkx>` prompt, whitespace tokenisation, backspace,
Ctrl-C and Ctrl-L, and a table of 37 registered commands. Commands are grouped
into core commands (`help`, `version`, `uptime`, `diag`, `shutdown`, ...),
applications (`calc`, `editor`, `files`, `monitor`) and subsystem commands
(`threads`, `scheduler`, `states`, `race`, `deadlock`, `lockorder`, `semtest`,
`ls`, `touch`, `cat`, `write`, `rm`, `diskinfo`, `vfstest`, `whoami`, `su`,
`permissions`, `access_test`, `usermode`, ...).

The four applications are ordinary kernel modules:

* the **calculator** uses a recursive-descent parser so that function calls and
  parentheses are handled naturally; it supports degrees/radians, base
  conversion, memory, history and combinatorics;
* the **editor** is a line editor over the VFS with a dirty flag and an
  unsaved-changes warning; its line array and file scratch buffer are allocated
  from the kernel heap on entry and released on exit;
* the **file manager** is a thin command layer over the VFS;
* the **monitor** samples live kernel structures once per second.

### 2.8 Trade-offs

| Decision | Benefit | Cost |
|---|---|---|
| Static heap arena | no frame allocator, deterministic | fixed 8 MiB |
| Identity paging | ring 3 works with little code | no isolation |
| Interrupt-frame switching | one switch path | all switches go through ISR |
| Coarse kernel lock | simple on UP | no SMP, interrupt latency |
| Logical principals | access checks without userland | not a security boundary |
| RAM disk | no block driver | volatile data |
| Serial mirroring | headless automated testing | small per-character cost |

---

## Chapter III – Implementation and Testing

### 3.1 Boot and initialisation

`boot/entry.asm` contains the Multiboot 2 header and a small stack, then calls
`kmain(magic, info_addr)`. `kernel/multiboot.cpp` validates the magic
`0x36d76289`, walks the tagged information structure and records the memory map,
bootloader name and available RAM.

The GDT is loaded with kernel and user selectors; the TSS is loaded and its
`esp0` field points at a kernel stack used when an interrupt arrives from ring 3.
The IDT has 256 gates: exceptions 0–31, IRQs remapped to `0x20–0x2f`, syscall
gate `0x80` and yield gate `0x81`. The PIC is remapped and only the timer,
keyboard, cascade and serial lines are unmasked.

The PIT is programmed to 100 Hz. `time::ticks()` is monotonic; `uptime_ms()`
derives milliseconds. The boot log records every step, for example:

```
[ boot ] multiboot2 handoff ok (magic=0x36d76289, info=0x...)
[ gdt  ] GDT/TSS loaded: kernel CS=0x8 DS=0x10, user CS=0x1b DS=0x23
[ heap ] kernel heap ready: 8192 KiB
[ mmu  ] paging enabled: 64 MiB identity mapped, 4 KiB pages, user bit set
[ vfs  ] ramdisk mounted: 256 KiB, 32 slots
[ sched] round robin scheduler online, quantum 5 ticks (50 ms)
Kernel initialisation complete. 4 threads running.
```

### 3.2 Interrupts and context switching

All ISR stubs push a dummy error code when needed and jump to `isr_common`, which
saves the general-purpose registers, loads the kernel data selector, calls
`interrupt_dispatch`, moves the returned pointer into `esp`, pops the registers
and executes `iret`. `Registers` mirrors the pushed layout exactly, so the C++
dispatcher can inspect and modify any saved register.

The timer handler increments the tick and per-thread CPU counters, wakes sleeping
threads, reaps terminated stacks and preempts when the quantum expires.
`int 0x81` calls the scheduler directly. `int 0x80` dispatches syscalls. Because
all three return a frame pointer, the same assembly path handles all of them.

### 3.3 Heap

The allocator uses 32-byte headers:

```
magic | size | prev_size | free | prev | next | checksum | reserved
```

Payloads are 16-byte aligned. Allocation walks the address-ordered list, splits
a block if the remainder is large enough, and updates boundary tags. Freeing
coalesces with the next and previous physical neighbours immediately. A freed
header is stamped `HEAP_MAGIC_DEAD`, so a second free is detected instead of
corrupting the list. `kfree` also validates the pointer range and the header
checksum; invalid frees increment a counter and return `false`.

`krealloc` allocates a new block, copies the smaller of the old and new sizes,
and frees the old block. `heap::self_test()` exercises alignment, payload
writability, double free, invalid free, coalescing, OOM, realloc preservation
and a stress loop, returning the number of failures. The `memtest` command
reports 8/8 checks passing.

### 3.4 Threads and scheduler implementation

`thread::create` allocates a slot and a stack, computes the aligned top, and
writes a `Registers` frame at `top - sizeof(Registers)` with `eip` set to
`thread_entry_stub`, `cs = KERNEL_CS`, `eflags = 0x202` and `esp = top`. The
stub enables interrupts, calls the entry function and then `thread::exit(0)`.
Slots are initialised with `memset`, which also clears `granted`, `wait_mutex`,
`in_ready` and the statistics.

The boot context calls `scheduler::start()`, which adopts it as the `shell`
thread. The idle thread is created by `scheduler::init()` with an 8 KiB stack and
a loop of `sti; hlt`. `heartbeat` sleeps for one second and increments a counter;
`memwatch` samples the heap every three seconds. Both are visible in `threads`
and in the scheduler statistics.

Two bugs were found and fixed here:

1. `adopt_boot_thread()` originally wrote into slot 0 and overwrote the idle
   thread. It now uses the normal slot allocator; the boot log reports four
   threads.
2. The NEW state was not observable in practice because `create()` immediately
   enqueued the thread. The optional `start_ready` parameter and
   `thread::start()` were added, and a `states` command was written to exercise
   the full lifecycle.

### 3.5 Mutex and deadlock handling

The mutex implementation went through the most interesting debugging of the
project. The first version handed ownership to a waiter in `unlock()` and
enqueued it, but the waiter's `lock()` then saw `m->owner == current` and took
the self-deadlock branch, returning without actually consuming the handoff. The
mutex remained owned by a thread that later terminated, so a subsequent
`lockorder` run hung with `owner=order-B`.

The fix has two parts:

1. a `granted` field on `Thread`; `unlock()` sets `next->granted = m`, and
   `lock()` checks and clears it before the self-deadlock test;
2. `mutex::release_all(Thread*)`, called from `thread::exit()`, which hands off
   or frees every mutex owned by the dying thread.

The same investigation produced the wait-for cycle detector. `would_deadlock`
walks the owner/wait chain with a guard and aborts an acquisition that would
close a cycle. The `deadlock` command deliberately creates such a cycle and
reports the detected condition, the recovery and the Coffman conditions. The
`lockorder` command acquires two mutexes in address order through
`lock_ordered()` and shows that the counter reaches 2 × N with no hang.

### 3.6 VFS, access control and ring 3

The VFS stores payloads in heap buffers and enforces the 256 KiB/64 KiB limits.
`write` supports append mode and returns `ENOSPC` without partial corruption.
Permissions are checked on every operation, and denials increment a counter that
`diskinfo` reports.

The access model has three principals. `su` changes a kernel variable, and
`access_test` constructs the full matrix (SYSTEM/USER/RESTRICTED ×
read/write/delete) and verifies that each result matches the policy. The test
reports 7/7 checks.

The ring 3 path required paging for the user bit, a TSS `esp0`, and an `iret`
frame with user selectors. The payload prints through syscall 1, reads ticks
through syscall 4 and exits through syscall 2. A deliberate `cli` raises #GP
(vector 13); `user_mode_abort()` rewrites the frame to return to ring 0 and the
shell continues. The implementation is honest about the fact that the identity
map leaves kernel memory user-accessible.

### 3.7 Applications and shell

All four applications are real. The calculator parses expressions with a
recursive-descent parser; the editor reads and writes the VFS; the file manager
calls the VFS directly; the monitor reads live scheduler, heap, VFS and security
counters. The shell registers commands only after their handlers exist, and
`help` prints the actual table (37 commands at the time of writing).

Two user-visible bugs were fixed in this area:

* `perm_string()` returned a pointer to one static buffer, so two masks printed
  in one `kprintf` showed the same value; it now rotates four buffers.
* `touch` printed synthetic permissions instead of the file's real masks; it
  now looks the file up after creation and prints the stored values.

The `kprintf` formatter was also rewritten during application work. The first
version mishandled `%llu`, left alignment and zero padding, which made the
thread and scheduler tables ragged. The new implementation separates field
emission from number conversion and supports `- 0 +`, width, precision and
`l`/`ll`/`z`.

### 3.8 Build system and hardware abstraction

The Makefile discovers common source files with `find`, compiles them into
`build/obj`, links `build/kernel.elf`, runs the ELF32/Multiboot2 checks and
invokes `grub-mkrescue` to produce `build/sharkxos.iso`. Targets: `all`,
`clean`, `iso`, `run`, `debug`, `check`, `test`, `help`. `run` defaults to a
headless QEMU with the serial console on stdio; `GUI=1` opens a window. `debug`
starts QEMU paused with a GDB stub on port 1234.

### 3.9 Test methodology

Because the development environment is WSL without a display, the kernel mirrors
its VGA output to COM1 and injects COM1 input into the keyboard queue. This
makes the serial port a complete substitute for screen and keyboard. Tests are
command scripts piped into QEMU:

```bash
( sleep 5; cat tests/smoke1.txt; sleep 12 ) | \
  timeout 40 qemu-system-i386 -cdrom build/sharkxos.iso -m 128M \
      -display none -serial stdio -monitor none -no-reboot > tests/logs/smoke1.log
```

`tools/run_all_tests.sh` runs eight suites and asserts explicit strings. A suite
fails on a missing expected line, on `PANIC`, or on `triple fault`. Self-checking
commands (`memtest`, `race`, `deadlock`, `lockorder`, `access_test`, `usermode`,
`vfstest`, `states`) compute their own verdict, so the harness checks a computed
`PASS`, not a hard-coded transcript.

### 3.10 Test results

All eight suites passed. Representative evidence:

| Test | Captured result |
|---|---|
| Boot | `Kernel initialisation complete. 4 threads running.` |
| `help` | `SharkXOS shell commands (37 registered)` |
| `version` | `SharkXOS version 1.0.0 (x86 32-bit (IA-32))`, `gcc 15.2.0` |
| `diag` | VGA read/write PASS, PIT ticking PASS (2 ticks in ~20 ms), paging enabled, 4 threads, 8192 KiB heap |
| `memtest` | `memtest: PASS (8/8 checks)` |
| `threads` | idle, shell (RUNNING), heartbeat (WAITING), memwatch (WAITING) |
| `scheduler` | quantum 5 ticks, 444 ticks, 94 context switches, 88 preemptions |
| `states` | `observed : NEW READY RUNNING WAITING TERMINATED`, `thread states: PASS` |
| `semtest` | both consumers WAITING, one signal releases exactly one, second releases the other, final count 0, `PASS` |
| `race 100` | without mutex 100/200 (100 lost), with mutex 200/200, `PASS` |
| `deadlock` | cycle found, acquisition aborted, both threads terminated, `PASS` |
| `lockorder` | counter 200, no deadlock, `PASS` |
| `access_test` | 7/7 checks, `PASS` |
| `usermode` | CPL=3 output, live ticks, exit 0, `PASS` |
| `usermode fault` | `vector 13`, kernel continues, `PASS` |
| `vfstest` | 4×64 KiB accepted, 5th `ENOSPC`, cleanup 0 bytes, `PASS` |
| Calculator | `2+3*4 = 14`, `sin(pi/2) = 1`, `5! = 120`, `sqrt(2) = 1.414213562`, `2^10 = 1024`, DEG, bin/hex, nCr/nPr, memory, errors |
| Editor | created, edited, `saved 78 bytes`, reloaded, `:q!`, file manager rename/delete; `heapinfo` shows the heap buffers returned on exit |
| Monitor | live uptime, scheduler, threads, heap, RAM disk, principal; returns to shell |

The exact logs are stored in `tests/logs/` and summarised in
[TEST_RESULTS.md](TEST_RESULTS.md). Two examples of real output:

```
race condition test: 2 threads x 100 increments = 200 expected
  without mutex   : counter = 100 (lost 100 updates)
  with mutex      : counter = 200 (lost 0 updates)
race test: PASS - unsynchronised update lost data, mutex fixed it
```

```
vfs exhaustion test: 256 KiB ramdisk, 64 KiB per write
  fill0.tmp: 64 KiB accepted (used 64 KiB)
  ...
  fill4.tmp: rejected with ENOSPC (no space left)
  accepted writes : 4 x 64 KiB
  ENOSPC denials  : 1
vfs exhaustion: PASS
```

### 3.11 Bugs found and fixed

| Bug | Symptom | Fix |
|---|---|---|
| Boot thread slot overwrite | idle thread corrupted | use `alloc_slot()` |
| Mutex handoff lost | `lockorder` hung, mutex owned by dead thread | `Thread::granted` + consume before self-deadlock check |
| Lock held after exit | future waiters blocked forever | `mutex::release_all()` from `thread::exit()` |
| Semaphore lost permit | a woken consumer saw count 0 and blocked again | `signal()` publishes the permit before waking; `semtest` verifies it |
| Formatter flags/`ll` | ragged tables, wrong large values | rewrote `kvsnprintf` field rendering |
| Static permission buffer | two masks printed identically | rotate four buffers |
| Synthetic `touch` output | permissions did not match file | read back the created file |
| Input queue overflow | long test scripts lost characters | 256 → 512 bytes |
| PIE/TEXTREL link warning | build warning | `-static -no-pie` |
| Hard-coded test path | harness failed outside the original directory | `cd "$(dirname "$0")/.."` |

---

## Chapter IV – Results, Limitations and Conclusion

### 4.1 Requirement fulfilment

| Requirement | Status |
|---|---|
| Boot + shell + ≥3 commands | met (37 commands) |
| Five thread states | met (`states` demo, twice per boot) |
| ≥2 kernel threads | met (idle, shell, heartbeat, memwatch) |
| Preemptive round-robin, real switch | met (timer, quantum, `iret` frame switch) |
| Mutex + semaphore | met |
| Race demo | met (100 lost updates vs exact 200) |
| Deadlock demo + prevention | met (cycle detection and lock ordering) |
| Heap: stats, OOM, double/invalid free | met (8/8 self-test) |
| VFS CRUD + required commands | met |
| Access control / `whoami` / `permissions` / `access_test` | met (logical model) |
| Four applications | met |
| Build targets | met (`all`, `clean`, `iso`, `run`, `debug`, plus `test`) |
| Documentation | met |
| Strict IA-32, no libc | met |
| Ring 3 with isolation | partial (real CPL=3, no memory isolation) |

### 4.2 Measured behaviour

The kernel boots in roughly one to two seconds under QEMU and reaches the shell
with four threads. At an uptime of 4.33 s the scheduler had processed 444 ticks
with 94 context switches and 88 preemptions, which is consistent with a 50 ms
quantum and a mostly idle shell. The heap self-test passes all eight checks. The
race test demonstrates the classic lost-update problem (100 of 200 increments
lost) and its disappearance under a mutex (200 of 200). The VFS accepts exactly
four 64 KiB files in a 256 KiB disk and rejects the fifth with `ENOSPC` without
leaving stale data. The access-control matrix passes 7/7. The ring 3 payload
executes at CPL=3 and a privileged instruction raises vector 13 without killing
the kernel.

### 4.3 Limitations

The main limitations are stated in detail in [KNOWN_ISSUES.md](KNOWN_ISSUES.md).
In summary:

1. **No memory isolation for ring 3.** The identity map gives user pages access
   to kernel memory. The privilege transition is real; the security boundary is
   not.
2. **Logical access control.** Permissions are enforced by kernel code, not by
   hardware, and can be bypassed by ring 3 code because of (1).
3. **No persistence.** The file system is a RAM disk.
4. **Uniprocessor only.** No SMP, APIC or per-CPU data.
5. **Fixed resources.** 8 MiB heap, 24 threads, 32 files, 16 mutexes.
6. **Terminated thread slots are not reclaimed.** Repeated demos eventually
   exhaust the 24 slots; the failure is graceful but a reboot is needed.
7. **No user program loader.** The ring 3 payload is compiled into the kernel.
8. **No shell history or arrow keys.** The keyboard driver ignores extended
   scancodes.
9. **A few compiler warnings remain** (unused helper functions).
10. **The project is not under version control.**

### 4.4 Lessons learned

* **One switch path is worth a lot.** Routing timer, yield and syscall through
  the same interrupt-frame mechanism removed duplicated assembly and made the
  scheduler easier to test.
* **Synchronisation bugs are subtle.** The mutex handoff bug only appeared after
  many iterations, and it manifested as a hang rather than a wrong value. The
  `granted` flag and release-on-exit are small changes with a large effect on
  robustness.
* **Self-checking commands are better than transcripts.** `memtest`, `race`,
  `vfstest` and `access_test` compute verdicts from live data. This made it
  possible to catch regressions instead of comparing screenshots.
* **Headless testing is a feature.** Serial mirroring turned out to be the most
  valuable infrastructure decision in the project; it allowed every change to be
  verified in seconds without a desktop.
* **Honesty is part of the deliverable.** The assignment's prohibition on fake
  output forced the design to expose real failure paths (`ENOSPC`, double free,
  deadlock, #GP) and to document what is missing.

### 4.5 Future work

The natural next steps are a physical frame allocator and per-process page
directories, an ELF32 loader with an `exec` syscall, an ATA PIO driver with a
persistent file system, extended keyboard support and shell history, priority
scheduling with a proper sleep queue, and CI running the existing suites on
every change.

### 4.6 Conclusion

SharkXOS meets the functional requirements of the assignment: it boots on real
IA-32 via Multiboot 2, schedules kernel threads preemptively, provides working
synchronisation with race and deadlock demonstrations, manages a heap with real
error detection, exposes a RAM-disk VFS with permissions, and ships four
applications and a 37-command shell. Every claim in this report is backed by a
captured serial log from the real kernel in QEMU. The two areas that fall short
of an ideal system — ring 3 memory isolation and persistent storage — are
documented openly rather than hidden, which is consistent with the project's
central constraint that nothing may be faked.

---

## AI-usage disclosure

An AI coding assistant (OpenCode, running the DeepSeek V4.1 Flash Free model) was
used during this project. Its role was:

* generating and refactoring source code from the design described in this
  report;
* diagnosing build and runtime failures from compiler output and serial logs;
* drafting documentation, including this report, from the implementation and
  the captured test logs;
* writing the headless test harness and command scripts.

The assistant did not have access to a reference implementation, and no test
result, log or measurement in this report was invented. Every result quoted in
Chapter III and Chapter IV was produced by booting the built ISO in QEMU and
capturing the serial output; the logs are retained in `tests/logs/`. Commands
that compute verdicts do so at runtime. Where a feature is incomplete (ring 3
isolation, persistence, SMP, user program loading), the limitation is stated
explicitly in this report and in `KNOWN_ISSUES.md`.

All architectural decisions, the final review of the code, and responsibility
for the submitted work remain with the author.

## References

1. Intel Corporation, *Intel 64 and IA-32 Architectures Software Developer's
   Manual*, volumes 1–3.
2. Multiboot2 Specification, GNU GRUB project.
3. GNU GRUB Manual 2.12.
4. NASM – The Netwide Assembler, documentation.
5. QEMU Emulator User Documentation, version 8.2.
6. GCC 15.2.0 manual, freestanding environment and IA-32 options.
7. A. Silberschatz, P. B. Galvin, G. Gagne, *Operating System Concepts*,
   10th edition, Wiley, 2018.
8. A. S. Tanenbaum, H. Bos, *Modern Operating Systems*, 4th edition, Pearson,
   2015.
9. R. H. Arpaci-Dusseau, A. C. Arpaci-Dusseau, *Operating Systems: Three Easy
   Pieces*, 2018.
10. E. W. Dijkstra, "Cooperating sequential processes", 1965 (semaphores and
    mutual exclusion).
