// SharkXOS - descriptor tables and interrupt plumbing
#pragma once
#include <kernel/common.h>

struct PACKED GdtEntry {
    u16 limit_low;
    u16 base_low;
    u8  base_middle;
    u8  access;
    u8  granularity;
    u8  base_high;
};

struct PACKED GdtPointer {
    u16 limit;
    u32 base;
};

// Segment selectors
#define KERNEL_CS 0x08
#define KERNEL_DS 0x10
#define USER_CS   0x18
#define USER_DS   0x20
#define TSS_SEL   0x28

struct PACKED TssEntry {
    u32 prev_tss;
    u32 esp0;
    u32 ss0;
    u32 esp1, ss1, esp2, ss2;
    u32 cr3, eip, eflags;
    u32 eax, ecx, edx, ebx, esp, ebp, esi, edi;
    u32 es, cs, ss, ds, fs, gs;
    u32 ldt;
    u16 trap;
    u16 iomap_base;
};

namespace gdt {
void init();
void set_kernel_stack(u32 esp0);
u32  kernel_stack();
} // namespace gdt

struct PACKED IdtEntry {
    u16 base_low;
    u16 sel;
    u8  always0;
    u8  flags;
    u16 base_high;
};

struct PACKED IdtPointer {
    u16 limit;
    u32 base;
};

// Register frame layout produced by boot/interrupts.asm
struct Registers {
    u32 ds;
    u32 edi, esi, ebp, esp, ebx, edx, ecx, eax;
    u32 int_no, err_code;
    u32 eip, cs, eflags;
    u32 useresp, ss;   // only present when an interrupt came from ring 3
};

typedef Registers* (*InterruptHandler)(Registers*);

#define IRQ_TIMER    0
#define IRQ_KEYBOARD 1
#define IRQ_CASCADE  2
#define IRQ_SERIAL   4

#define VEC_TIMER    32
#define VEC_KEYBOARD 33
#define VEC_SERIAL   36
#define VEC_SYSCALL  0x80
#define VEC_YIELD    0x81

namespace idt {
void init();
void set_gate(u8 num, u32 base, u16 sel, u8 flags);
void enable_irq(u8 irq);
void disable_irq(u8 irq);
} // namespace idt

extern "C" void gdt_flush(u32 gdt_ptr);
extern "C" void tss_flush();
extern "C" void idt_load(u32 idt_ptr);

extern "C" Registers* interrupt_dispatch(Registers* r);
