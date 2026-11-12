# SharkXOS – Build and Run Instructions

Everything below was executed on **Ubuntu 24.04.5 LTS under WSL2** (kernel
6.18.54.1-microsoft-standard-WSL2) with QEMU 8.2.2, GRUB 2.12, NASM 2.16.01 and
GCC 15.2.0 (`i686-elf`). Other Linux distributions should work with equivalent
packages.

---

## 1. Packages

```bash
sudo apt update
sudo apt install -y build-essential nasm xorriso mtools \
                    grub-pc-bin grub-common grub2-common \
                    qemu-system-x86 gcc-multilib g++-multilib unzip
```

`grub-pc-bin` is required even on UEFI machines because the ISO uses the BIOS
boot path. `xorriso` and `mtools` are used by `grub-mkrescue`.

If `apt` fails with HTTP errors, switch the Ubuntu sources to HTTPS:

```bash
sudo sed -i 's|http://archive.ubuntu.com|https://archive.ubuntu.com|g' \
            /etc/apt/sources.list /etc/apt/sources.list.d/*.list 2>/dev/null
sudo apt update
```

## 2. Cross compiler

The build prefers a real `i686-elf` cross compiler and falls back to the host
`gcc -m32` if none is found. The cross compiler is strongly recommended: it
removes the host headers from the include path and guarantees 32-bit
freestanding code generation.

This project used the prebuilt **i686-elf-tools** package (GCC 15.2.0, binutils
2.45):

```bash
# download i686-elf-tools-linux.zip from the lordmilko/i686-elf-tools releases
cd /tmp
unzip -q i686-elf-tools-linux.zip -d /opt/cross/i686-elf

# make the tools visible on PATH
for f in /opt/cross/i686-elf/bin/*; do
  sudo ln -sf "$f" "/usr/local/bin/$(basename "$f")"
done

i686-elf-g++ --version   # should print GCC 15.2.0 or newer
```

Alternatively, build the cross compiler from source with
`binutils` + `gcc` configured for `--target=i686-elf --without-headers
--enable-languages=c,c++`. This takes considerably longer.

The build reports which toolchain it selected:

```
[build] using cross compiler: i686-elf-g++
```

or

```
[build] i686-elf cross compiler not found - using host gcc -m32 (freestanding)
```

## 3. Building

From the project root:

```bash
make clean      # remove build/
make            # compile, link, verify ELF32 + Multiboot2, create ISO
make iso        # only (re)create build/sharkxos.iso
```

`make` performs these steps:

1. compiles every `.cpp` under `kernel drivers memory process sync fs security
   lib apps` with `-march=i686 -ffreestanding ...`;
2. assembles every `.asm` under `boot/` with NASM (`elf32`);
3. links with `linker.ld`, `-nostdlib -static -no-pie`;
4. runs `check`: `file`, `readelf` (must be ELF32) and
   `grub-file --is-x86-multiboot2`;
5. builds the ISO directory and calls `grub-mkrescue`.

Artifacts:

```
build/kernel.elf      ELF32 executable, Intel 80386, ~383 KiB
build/sharkxos.iso    bootable BIOS ISO, ~5.5 MB
build/obj/            object files and dependency files
```

Expected tail of a successful build:

```
  LD   build/kernel.elf
  OK   build/kernel.elf built
  CHK  kernel is ELF32:
       build/kernel.elf: ELF 32-bit LSB executable, Intel 80386, ...
       multiboot2 header: OK
       readelf: ELF32 OK
  ISO  build/sharkxos.iso
  OK   build/sharkxos.iso created
```

## 4. Running

### Headless (default, works in WSL/CI)

```bash
make run
```

This starts:

```bash
qemu-system-i386 -cdrom build/sharkxos.iso -m 128M \
                 -serial stdio -monitor none -no-reboot -display none
```

The VGA console is mirrored to the serial port and serial input is fed into the
kernel keyboard queue, so the shell is fully usable from the terminal. Quit with
`Ctrl-C` (or close the terminal).

### With a window

On a machine with a desktop:

```bash
make run GUI=1
```

This drops `-display none` and opens the normal QEMU window. The VGA output is
then visible directly; the serial console still mirrors it.

### Direct QEMU invocation

```bash
qemu-system-i386 -cdrom build/sharkxos.iso -m 128M -serial stdio
```

### Debugging with GDB

```bash
make debug
```

QEMU starts **paused** with a GDB stub on TCP port 1234. In another terminal:

```bash
i686-elf-gdb build/kernel.elf \
  -ex 'set architecture i386' \
  -ex 'target remote :1234' \
  -ex 'break kmain' \
  -ex 'continue'
```

Useful GDB commands: `info registers`, `x/10i $eip`, `bt`, `monitor info
registers`. Continue with `c`; the kernel only starts executing after the first
`continue`.

## 5. Automated tests

```bash
make test                 # boot the ISO and run the smoke suite
tools/run_all_tests.sh    # run every suite, logs in tests/logs/
tools/run_all_tests.sh smoke1 sync3
```

The harness pipes a command script into QEMU over the serial console and checks
for explicit expected strings. Because QEMU is stopped by `timeout`, exit code
`124` is normal for suites that do not power the machine off. A suite fails if
an expected string is missing or if `PANIC` appears in the log.

Individual runs:

```bash
tools/long_test.sh build/my.log tests/smoke1.txt 40 5
#                  log           commands       secs delay
```

## 6. Verifying the image manually

```bash
file build/kernel.elf
readelf -h build/kernel.elf | grep -E 'Class|Type|Machine'
grub-file --is-x86-multiboot2 build/kernel.elf && echo OK
```

Expected:

```
ELF 32-bit LSB executable, Intel 80386, ...
  Class:   ELF32
  Type:    EXEC
  Machine: Intel 80386
multiboot2 header: OK
```

## 7. Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `i686-elf-g++: command not found` | cross compiler not on `PATH`; symlink it or let the host fallback run |
| `grub-mkrescue: command not found` | install `grub-pc-bin grub-common` |
| `xorriso : FAILED` / ISO not created | install `xorriso` and `mtools` |
| `qemu-system-i386: command not found` | install `qemu-system-x86` |
| `Could not open 'build/sharkxos.iso'` | run `make` first; `make clean` deletes the ISO |
| QEMU opens no window under WSL | expected; use the default headless mode |
| Serial log shows no prompt | wait ~2 s; GRUB takes a moment, or increase the input delay |
| Test exits 124 | normal: `timeout` killed QEMU |
| Long command files overflow input | the keyboard queue is 512 bytes; split the script |
| Build stops with a broken pipe | do not pipe `make` into `head`; pipe through `tail` instead |
| `TEXTREL` / PIE warning at link | ensure `-static -no-pie` is present (already in the Makefile) |

## 8. Clean rebuild from scratch

```bash
make clean
make
make test
```

This is the sequence used to validate the delivered tree before writing
[TEST_RESULTS.md](TEST_RESULTS.md).
