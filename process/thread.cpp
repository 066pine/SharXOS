// SharkXOS - kernel thread management
#include <process/thread.h>
#include <process/scheduler.h>
#include <memory/heap.h>
#include <sync/mutex.h>
#include <kernel/descriptors.h>
#include <kernel/string.h>
#include <kernel/time.h>
#include <drivers/console.h>

extern "C" void kernel_lock();
extern "C" void kernel_unlock();

namespace {

Thread g_threads[THREAD_MAX];
u32    g_next_id = 1;
u32    g_created = 0;
Thread* g_current = nullptr;

Thread* alloc_slot() {
    for (int i = 0; i < THREAD_MAX; i++) {
        if (g_threads[i].id == 0) return &g_threads[i];
    }
    return nullptr;
}

} // namespace

const char* thread_state_name(ThreadState s) {
    switch (s) {
    case ThreadState::NEW: return "NEW";
    case ThreadState::READY: return "READY";
    case ThreadState::RUNNING: return "RUNNING";
    case ThreadState::WAITING: return "WAITING";
    case ThreadState::TERMINATED: return "TERMINATED";
    }
    return "?";
}

// Called (via thread_entry_stub) the first time a new thread is scheduled.
extern "C" void thread_trampoline() {
    Thread* t = thread::current();
    if (!t) {
        for (;;) { kstd::cli(); kstd::hlt(); }
    }
    kstd::sti();
    if (t->entry) t->entry(t->arg);
    thread::exit(0);
}

namespace thread {

void init() {
    memset(g_threads, 0, sizeof(g_threads));
    g_next_id = 1;
    g_created = 0;
    g_current = nullptr;
} // init()
Thread* create(const char* name, ThreadEntry entry, void* arg, u32 stack_size,
               bool start_ready) {
    kernel_lock();

    Thread* t = alloc_slot();
    if (!t) { kernel_unlock(); return nullptr; }
    if (stack_size < 4096) stack_size = 4096;

    u8* stack = (u8*)heap::kmalloc(stack_size);
    if (!stack) { kernel_unlock(); return nullptr; }
    memset(stack, 0, 64);

    u32 top = (u32)(stack + stack_size);
    top &= 0xFFFFFFF0u;

    Registers* frame = (Registers*)(top - sizeof(Registers));
    memset(frame, 0, sizeof(Registers));
    frame->ds = KERNEL_DS;
    frame->int_no = 0;
    frame->err_code = 0;
    frame->eip = (u32)thread_entry_stub;
    frame->cs = KERNEL_CS;
    frame->eflags = 0x202;    // IF set, reserved bit 1 set
    frame->esp = top;

    memset(t, 0, sizeof(Thread));
    t->id = g_next_id++;
    strncpy(t->name, name ? name : "thread", THREAD_NAME_MAX - 1);
    t->name[THREAD_NAME_MAX - 1] = 0;
    t->state = start_ready ? ThreadState::READY : ThreadState::NEW;
    t->esp = (u32)frame;
    t->stack_base = (u32)stack;
    t->stack_size = stack_size;
    t->entry = entry;
    t->arg = arg;
    t->created_tick = (u32)time::ticks();
    t->stack_freed = false;

    g_created++;
    if (start_ready) scheduler::add(t);

    kernel_unlock();
    return t;
}

// NEW -> READY. Threads created with start_ready=false sit in state NEW until
// this is called, which makes the NEW state observable and testable.
void start(Thread* t) {
    if (!t) return;
    kernel_lock();
    if (t->state == ThreadState::NEW) {
        t->state = ThreadState::READY;
        scheduler::add(t);
    }
    kernel_unlock();
}

Thread* current() { return g_current; }

void set_current(Thread* t) { g_current = t; }

u32 total_created() { return g_created; }

Thread* by_id(u32 id) {
    for (int i = 0; i < THREAD_MAX; i++) {
        if (g_threads[i].id == id) return &g_threads[i];
    }
    return nullptr;
}

u32 count() {
    u32 n = 0;
    for (int i = 0; i < THREAD_MAX; i++) if (g_threads[i].id) n++;
    return n;
}

Thread* at(u32 index) {
    u32 n = 0;
    for (int i = 0; i < THREAD_MAX; i++) {
        if (!g_threads[i].id) continue;
        if (n == index) return &g_threads[i];
        n++;
    }
    return nullptr;
}

void set_state(Thread* t, ThreadState s) {
    if (!t) return;
    if (t->state == ThreadState::TERMINATED && s != ThreadState::TERMINATED) return;
    t->state = s;
}

void yield() {
    __asm__ volatile("int $0x81" ::: "memory");
}

void exit(u32 code) {
    Thread* t = current();
    if (t) {
        mutex::release_all(t);        // never leave a mutex owned by a dead thread
        t->state = ThreadState::TERMINATED;
        t->exit_code = code;
    }
    // Switch away for good; the scheduler will never pick a TERMINATED thread.
    yield();
    // Should never get here, but staying alive is safer than corrupting memory.
    for (;;) { kstd::cli(); kstd::hlt(); }
}

void sleep_ms(u32 ms) {
    Thread* t = current();
    if (!t || ms == 0) { yield(); return; }
    t->sleeping = true;
    t->wake_tick = (u32)time::ticks() + (ms * time::hz()) / 1000u + 1;
    t->state = ThreadState::WAITING;
    yield();
    t->sleeping = false;
}

void wake(Thread* t) {
    if (!t) return;
    if (t->state == ThreadState::WAITING) {
        t->sleeping = false;
        t->state = ThreadState::READY;
        scheduler::add(t);
    }
}

void adopt_boot_thread(const char* name) {
    Thread* t = alloc_slot();
    if (!t) return;
    memset(t, 0, sizeof(Thread));
    t->id = g_next_id++;
    strncpy(t->name, name ? name : "boot", THREAD_NAME_MAX - 1);
    t->name[THREAD_NAME_MAX - 1] = 0;
    t->state = ThreadState::RUNNING;
    t->stack_base = 0;
    t->stack_size = 0;
    t->created_tick = 0;
    t->stack_freed = false;
    g_current = t;
    g_created++;
}

} // namespace thread
