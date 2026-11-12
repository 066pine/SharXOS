// SharkXOS - preemptive round robin scheduler
#pragma once
#include <kernel/common.h>
#include <kernel/descriptors.h>
#include <process/thread.h>

struct SchedStats {
    u64 ticks;
    u64 context_switches;
    u64 preemptions;
    u64 voluntary_yields;
    u32 quantum_ticks;
    u32 ready_count;
    u32 thread_count;
    u32 running_id;
    char running_name[THREAD_NAME_MAX];
};

namespace scheduler {
void init();
void start();                    // adopt boot thread and begin scheduling
Registers* tick(Registers* r);   // timer interrupt hook
Registers* schedule(Registers* r);
void yield();                    // int 0x81
void add(Thread* t);
void remove(Thread* t);
SchedStats stats();
void set_quantum(u32 ticks);
bool running();
} // namespace scheduler

extern "C" void sched_switch_stub();  // not used, kept for ABI clarity
