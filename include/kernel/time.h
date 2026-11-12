// SharkXOS - time keeping (PIT)
#pragma once
#include <kernel/common.h>

namespace time {
void init();
void tick();                    // called from the timer IRQ
u64  ticks();
u32  hz();
u64  uptime_ms();
void sleep_ms(u32 ms);
void busy_wait_ms(u32 ms);
} // namespace time
