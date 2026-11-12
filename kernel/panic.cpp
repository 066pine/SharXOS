// SharkXOS - kernel panic handler
#include <kernel/panic.h>
#include <drivers/console.h>
#include <kernel/time.h>

static u32 g_panic_count = 0;

NORETURN void kernel_panic(const char* message) {
    kstd::cli();
    g_panic_count++;
    console::set_color(VGA_WHITE, VGA_RED);
    console::write("\n*** KERNEL PANIC ***\n");
    console::write(message);
    console::write("\n");
    console::reset_color();
    kprintf("System halted. Uptime: %llu ms\n", (unsigned long long)time::uptime_ms());

    for (;;) {
        kstd::cli();
        kstd::hlt();
    }
}

NORETURN void kernel_panic_regs(const char* message, Registers* r) {
    kstd::cli();
    g_panic_count++;
    console::set_color(VGA_WHITE, VGA_RED);
    console::write("\n*** KERNEL PANIC: ");
    console::write(message);
    console::write(" ***\n");
    console::reset_color();

    if (r) {
        kprintf("int=%u err=0x%x eip=0x%x cs=0x%x eflags=0x%x\n",
                r->int_no, r->err_code, r->eip, r->cs, r->eflags);
        kprintf("eax=0x%x ebx=0x%x ecx=0x%x edx=0x%x\n", r->eax, r->ebx, r->ecx, r->edx);
        kprintf("esi=0x%x edi=0x%x ebp=0x%x esp=0x%x\n", r->esi, r->edi, r->ebp, r->esp);
        u32 cr2;
        __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
        kprintf("cr2=0x%x\n", cr2);
    }
    console::write("System halted.\n");

    for (;;) {
        kstd::cli();
        kstd::hlt();
    }
}

namespace panic {
u32 count() { return g_panic_count; }
}
