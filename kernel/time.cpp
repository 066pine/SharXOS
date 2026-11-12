// SharkXOS - Programmable Interval Timer (PIT) time source
#include <kernel/time.h>
#include <kernel/descriptors.h>
#include <process/scheduler.h>

#define PIT_FREQUENCY 1193182u
#define TICK_HZ 100u

namespace time {

static volatile u64 g_ticks = 0;

void init() {
    u32 divisor = PIT_FREQUENCY / TICK_HZ;
    kstd::outb(0x43, 0x36);                      // channel 0, lobyte/hibyte, mode 3
    kstd::outb(0x40, (u8)(divisor & 0xFF));
    kstd::outb(0x40, (u8)((divisor >> 8) & 0xFF));
    g_ticks = 0;
}

void tick() { g_ticks++; }

u64 ticks() { return g_ticks; }
u32 hz() { return TICK_HZ; }
u64 uptime_ms() { return (g_ticks * 1000ull) / TICK_HZ; }

void sleep_ms(u32 ms) {
    u64 target = g_ticks + (u64)((ms * TICK_HZ) / 1000u);
    if (target <= g_ticks) target = g_ticks + 1;
    while (g_ticks < target) {
        __asm__ volatile("sti; hlt" ::: "memory");
    }
}

void busy_wait_ms(u32 ms) {
    u64 target = g_ticks + (u64)((ms * TICK_HZ) / 1000u);
    if (target <= g_ticks) target = g_ticks + 1;
    while (g_ticks < target) { kstd::pause(); }
}

} // namespace time
