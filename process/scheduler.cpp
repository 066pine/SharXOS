// SharkXOS - preemptive round robin scheduler
#include <process/scheduler.h>
#include <process/thread.h>
#include <memory/heap.h>
#include <kernel/time.h>
#include <kernel/string.h>
#include <drivers/console.h>

extern "C" void kernel_lock();
extern "C" void kernel_unlock();

namespace {

Thread* g_ready[THREAD_MAX];
u32     g_head = 0;
u32     g_tail = 0;
u32     g_count = 0;

u32     g_quantum = 5;          // ticks per time slice (50 ms at 100 Hz)
u32     g_quantum_left = 5;
bool    g_running = false;
Thread* g_idle = nullptr;
SchedStats g_stats;

void ready_push(Thread* t) {
    if (!t) return;
    if (t->in_ready) return;
    if (g_count >= THREAD_MAX) return;
    t->in_ready = true;
    g_ready[g_tail] = t;
    g_tail = (g_tail + 1) % THREAD_MAX;
    g_count++;
}

Thread* ready_pop() {
    if (g_count == 0) return nullptr;
    Thread* t = g_ready[g_head];
    g_head = (g_head + 1) % THREAD_MAX;
    g_count--;
    if (t) t->in_ready = false;
    return t;
}

void idle_entry(void*) {
    for (;;) {
        kstd::sti();
        kstd::hlt();
    }
}

void reap_terminated(Thread* keep) {
    for (u32 i = 0; i < THREAD_MAX; i++) {
        Thread* t = thread::at(i);
        if (!t) continue;
        if (t == keep) continue;
        if (t->state != ThreadState::TERMINATED) continue;
        if (t->stack_freed) continue;
        if (!t->stack_base) continue;
        heap::kfree((void*)t->stack_base);
        t->stack_base = 0;
        t->stack_freed = true;
    }
}

} // namespace

namespace scheduler {

void init() {
    g_head = g_tail = g_count = 0;
    g_quantum = 5;
    g_quantum_left = g_quantum;
    g_running = false;
    memset(&g_stats, 0, sizeof(g_stats));
    memset(g_ready, 0, sizeof(g_ready));

    thread::init();
    g_idle = thread::create("idle", idle_entry, nullptr, 8 * 1024);
    if (g_idle) g_idle->state = ThreadState::READY;
}

void start() {
    thread::adopt_boot_thread("shell");
    g_running = true;
}

void add(Thread* t) {
    if (!t) return;
    kernel_lock();
    ready_push(t);
    kernel_unlock();
}

void remove(Thread* t) {
    if (!t) return;
    kernel_lock();
    t->in_ready = false;
    u32 n = g_count;
    u32 idx = g_head;
    u32 out = 0;
    Thread* rebuilt[THREAD_MAX];
    for (u32 i = 0; i < n; i++) {
        Thread* x = g_ready[(idx + i) % THREAD_MAX];
        if (x && x != t) rebuilt[out++] = x;
    }
    g_head = 0;
    g_tail = out % THREAD_MAX;
    g_count = out;
    for (u32 i = 0; i < out; i++) g_ready[i] = rebuilt[i];
    kernel_unlock();
}

bool running() { return g_running; }
void set_quantum(u32 ticks) { if (ticks) g_quantum = ticks; }

Registers* schedule(Registers* r) {
    Thread* cur = thread::current();

    if (cur) {
        cur->esp = (u32)r;
        if (cur->state == ThreadState::RUNNING) {
            cur->state = ThreadState::READY;
            ready_push(cur);
        }
    }

    Thread* next = nullptr;
    u32 scan = g_count;
    while (scan--) {
        Thread* cand = ready_pop();
        if (!cand) break;
        if (cand->state != ThreadState::READY) continue;
        if (cand == cur) {
            ready_push(cand);      // rotate to the back of the queue
            continue;
        }
        next = cand;
        break;
    }

    if (!next && cur && cur->state == ThreadState::READY) next = cur;
    if (!next) next = g_idle;
    if (!next) {
        // Absolutely nothing to run - halt until an interrupt arrives.
        for (;;) { kstd::sti(); kstd::hlt(); }
    }

    next->state = ThreadState::RUNNING;
    if (next != cur) {
        g_stats.context_switches++;
        next->switches++;
    }
    thread::set_current(next);
    g_quantum_left = g_quantum;

    return (Registers*)next->esp;
}

Registers* tick(Registers* r) {
    if (!g_running) return r;

    g_stats.ticks++;
    Thread* cur = thread::current();
    if (cur) cur->cpu_ticks++;

    // wake sleeping threads
    u32 now = (u32)time::ticks();
    for (u32 i = 0; i < THREAD_MAX; i++) {
        Thread* t = thread::at(i);
        if (!t) continue;
        if (t->state == ThreadState::WAITING && t->sleeping) {
            if ((i32)(now - t->wake_tick) >= 0) thread::wake(t);
        }
    }

    reap_terminated(cur);

    if (g_quantum_left > 0) g_quantum_left--;
    if (g_quantum_left == 0) {
        g_stats.preemptions++;
        return schedule(r);
    }
    return r;
}

void yield() {
    __asm__ volatile("int $0x81" ::: "memory");
}

SchedStats stats() {
    SchedStats s = g_stats;
    s.ready_count = g_count;
    s.thread_count = thread::count();
    s.quantum_ticks = g_quantum;
    Thread* cur = thread::current();
    if (cur) {
        s.running_id = cur->id;
        strncpy(s.running_name, cur->name, THREAD_NAME_MAX - 1);
        s.running_name[THREAD_NAME_MAX - 1] = 0;
    } else {
        s.running_id = 0;
        s.running_name[0] = 0;
    }
    return s;
}

} // namespace scheduler
