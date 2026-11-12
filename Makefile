# SharkXOS - build system
# Target: x86 32-bit (IA-32), Multiboot2, GRUB 2, QEMU

CROSS_PREFIX := $(shell command -v i686-elf-g++ >/dev/null 2>&1 && echo i686-elf- || echo "")

ifeq ($(CROSS_PREFIX),)
  CC      := gcc
  CXX     := g++
  LD      := ld
  HOST32  := -m32
  $(info [build] i686-elf cross compiler not found - using host gcc -m32 (freestanding))
else
  CC      := $(CROSS_PREFIX)gcc
  CXX     := $(CROSS_PREFIX)g++
  LD      := $(CROSS_PREFIX)ld
  HOST32  :=
  $(info [build] using cross compiler: $(CXX))
endif

AS      := nasm
BUILD   := build
OBJDIR  := $(BUILD)/obj
KERNEL  := $(BUILD)/kernel.elf
ISO     := $(BUILD)/sharkxos.iso
ISODIR  := $(BUILD)/iso

CXXFLAGS := $(HOST32) -std=c++17 -O2 -g \
            -ffreestanding -fno-exceptions -fno-rtti -fno-builtin \
            -fno-stack-protector -fno-pic -fno-pie -fno-omit-frame-pointer \
            -fno-asynchronous-unwind-tables -fno-unwind-tables \
            -fno-threadsafe-statics -fno-use-cxa-atexit \
            -march=i686 -mno-sse -mno-sse2 -mno-mmx \
            -Wall -Wextra -Wno-unused-parameter \
            -Iinclude -MMD -MP

ASFLAGS := -f elf32 -g -F dwarf

LDFLAGS := $(HOST32) -T linker.ld -nostdlib -ffreestanding -static -no-pie \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,-no-pie

CSOURCES   := $(shell find kernel drivers memory process sync fs security lib apps -name '*.cpp' 2>/dev/null | sort)
ASMSOURCES := $(shell find boot -name '*.asm' 2>/dev/null | sort)

OBJS := $(patsubst %.cpp,$(OBJDIR)/%.o,$(CSOURCES)) \
        $(patsubst %.asm,$(OBJDIR)/%.o,$(ASMSOURCES))

.PHONY: all clean iso run debug check test help

# QEMU. WSL/CI hosts usually have no X display, so headless is the default and
# the VGA console is mirrored to the serial port. Use `make run GUI=1` to get a
# real window on a machine with a desktop.
QEMU      ?= qemu-system-i386
QEMUFLAGS := -cdrom $(ISO) -m 128M -serial stdio -monitor none -no-reboot
ifeq ($(GUI),1)
  QEMUDISPLAY :=
else
  QEMUDISPLAY := -display none
endif

all: $(KERNEL) check iso

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "  CXX  $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.asm
	@mkdir -p $(dir $@)
	@echo "  ASM  $<"
	@$(AS) $(ASFLAGS) $< -o $@

$(KERNEL): $(OBJS) linker.ld
	@mkdir -p $(BUILD)
	@echo "  LD   $@"
	@$(CXX) $(LDFLAGS) -o $@ $(OBJS) -lgcc
	@echo "  OK   $(KERNEL) built"

check: $(KERNEL)
	@echo "  CHK  kernel is ELF32:"
	@file $(KERNEL) | sed 's/^/       /'
	@if command -v grub-file >/dev/null 2>&1; then \
	  grub-file --is-x86-multiboot2 $(KERNEL) && echo "       multiboot2 header: OK" || \
	  (echo "       multiboot2 header: MISSING" && exit 1); \
	fi
	@readelf -h $(KERNEL) | grep -q 'ELF32' && echo "       readelf: ELF32 OK" || \
	  (echo "       readelf: NOT ELF32" && exit 1)

$(ISO): $(KERNEL) grub/grub.cfg
	@echo "  ISO  $@"
	@rm -rf $(ISODIR)
	@mkdir -p $(ISODIR)/boot/grub
	@cp $(KERNEL) $(ISODIR)/boot/kernel.elf
	@cp grub/grub.cfg $(ISODIR)/boot/grub/grub.cfg
	@grub-mkrescue -o $(ISO) $(ISODIR) >/dev/null 2>&1 || \
	  grub-mkrescue -o $(ISO) $(ISODIR)
	@echo "  OK   $(ISO) created"

iso: $(ISO)

run: $(ISO)
	@echo "  RUN  qemu-system-i386 ($(if $(filter 1,$(GUI)),windowed,headless serial console))"
	@$(QEMU) $(QEMUFLAGS) $(QEMUDISPLAY)

debug: $(ISO)
	@echo "  DBG  qemu-system-i386 paused, GDB stub on tcp::1234"
	@echo "       attach with: i686-elf-gdb build/kernel.elf -ex 'target remote :1234'"
	@$(QEMU) $(QEMUFLAGS) $(QEMUDISPLAY) -s -S

test: $(ISO)
	@bash tools/boot_test.sh build/boot.log 5 tests/smoke1.txt
	@echo "  LOG  build/boot.log"

clean:
	@rm -rf $(BUILD)
	@echo "  CLN  build/ removed"

help:
	@echo "SharkXOS build targets:"
	@echo "  make        build kernel, verify format, create ISO"
	@echo "  make iso    build the bootable ISO"
	@echo "  make run    boot the ISO in QEMU (headless serial console)"
	@echo "  make run GUI=1   boot with a QEMU window (needs a desktop)"
	@echo "  make debug  boot QEMU paused with a GDB stub on :1234"
	@echo "  make test   boot the ISO and run the smoke test over serial"
	@echo "  make clean  remove build artifacts"

-include $(OBJS:.o=.d)
