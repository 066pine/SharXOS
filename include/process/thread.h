// SharkXOS - kernel threads
#pragma once
#include <kernel/common.h>

enum class ThreadState : u8 {
    NEW = 0,
    READY,
    RUNNING,
    WAITING,
    TERMINATED
};

const char* thread_state_name(ThreadState s);

#define THREAD_MAX 24
#define THREAD_NAME_MAX 24
#define THREAD_STACK_SIZE (16 * 1024)

typedef void (*ThreadEntry)(void* arg);

struct Thread {
    u32          id;
    char         name[THREAD_NAME_MAX];
    ThreadState  state;
    u32          esp;            // saved kernel stack pointer (interrupt frame)
    u32          stack_base;      // allocated stack bottom
    u32          stack_size;
    ThreadEntry  entry;
    void*        arg;
    u32          wake_tick;       // for sleep
    bool         sleeping;
    u32          cpu_ticks;       // ticks spent running
    u32          switches;
    bool         detached;
    bool         stack_freed;
    bool         in_ready;       // currently linked in the scheduler ready queue
    struct Mutex* wait_mutex;     // mutex this thread is blocked on
    struct Mutex* granted;        // mutex handed over directly by unlock()
    u32          created_tick;
    u32          exit_code;
};

namespace thread {
void   init();
Thread* create(const char* name, ThreadEntry entry, void* arg, u32 stack_size = THREAD_STACK_SIZE,
               bool start_ready = true);
void   start(Thread* t);            // NEW -> READY and enqueue
Thread* current();
void   set_current(Thread* t);
Thread* by_id(u32 id);
u32    count();
Thread* at(u32 index);
void   yield();
NORETURN void exit(u32 code = 0);
void   sleep_ms(u32 ms);
void   wake(Thread* t);
void   set_state(Thread* t, ThreadState s);
const char* state_name(ThreadState s);
// Register the boot execution context as the initial thread (called from kmain)
void   adopt_boot_thread(const char* name);
u32    total_created();
} // namespace thread

extern "C" void thread_trampoline();
extern "C" void thread_entry_stub();
