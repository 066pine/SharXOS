// SharkXOS - global kernel state
#pragma once
#include <kernel/common.h>

namespace kernel {
u32 heartbeat_count();
u32 memwatch_samples();
const char* build_date();
const char* build_time();
const char* compiler();
u64 boot_tick();
bool ready();
void set_ready(bool v);
u32 idle_halts();
void count_idle_halt();
} // namespace kernel
