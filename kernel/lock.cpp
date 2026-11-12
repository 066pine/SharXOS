// SharkXOS - kernel critical section helpers
//
// A single-CPU kernel can use "interrupts off" as its mutual exclusion
// primitive.  kernel_lock() is nestable: the interrupt flag of the outermost
// acquisition is restored when the outermost release happens.  Because IF is
// cleared for the whole nested section, timer driven preemption cannot happen
// while a kernel data structure is being mutated.
#include <kernel/common.h>

static u32 g_depth = 0;
static u32 g_flags = 0;

extern "C" void kernel_lock() {
    u32 f = kstd::irq_save();
    if (g_depth++ == 0) g_flags = f;
}

extern "C" void kernel_unlock() {
    if (g_depth && --g_depth == 0) {
        kstd::irq_restore(g_flags);
    }
}

extern "C" u32 kernel_lock_depth() { return g_depth; }
