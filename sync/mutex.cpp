// SharkXOS - mutexes, deadlock detection and counting semaphores
#include <sync/mutex.h>
#include <process/scheduler.h>
#include <kernel/string.h>
#include <kernel/time.h>
#include <drivers/console.h>

extern "C" void kernel_lock();
extern "C" void kernel_unlock();

namespace {

Mutex g_mutexes[MUTEX_MAX];
u32   g_mutex_count = 0;
u32   g_next_id = 1;

// Walk the wait-for graph: does `requester` already wait (transitively)
// for a mutex owned by a thread that waits for `m`?
bool would_deadlock(Mutex* m, Thread* requester) {
    Thread* owner = m->owner;
    int guard = 0;
    while (owner && guard++ < 64) {
        if (owner == requester) return true;
        Mutex* waiting_for = owner->wait_mutex;
        if (!waiting_for) return false;
        owner = waiting_for->owner;
    }
    return false;
}

void enqueue_waiter(Mutex* m, Thread* t) {
    if (m->waiter_count >= MUTEX_WAITERS_MAX) return;
    m->waiters[m->waiter_count++] = t;
}

Thread* dequeue_waiter(Mutex* m) {
    if (m->waiter_count == 0) return nullptr;
    Thread* t = m->waiters[0];
    for (u32 i = 1; i < m->waiter_count; i++) m->waiters[i - 1] = m->waiters[i];
    m->waiter_count--;
    m->waiters[m->waiter_count] = nullptr;
    return t;
}

} // namespace

namespace mutex {

void init() {
    memset(g_mutexes, 0, sizeof(g_mutexes));
    g_mutex_count = 0;
    g_next_id = 1;
}

Mutex* create(const char* name, bool recursive) {
    kernel_lock();
    Mutex* m = nullptr;
    for (u32 i = 0; i < MUTEX_MAX; i++) {
        if (!g_mutexes[i].used) { m = &g_mutexes[i]; break; }
    }
    if (m) {
        memset(m, 0, sizeof(Mutex));
        m->id = g_next_id++;
        strncpy(m->name, name ? name : "mutex", sizeof(m->name) - 1);
        m->used = true;
        m->recursive = recursive;
        g_mutex_count++;
    }
    kernel_unlock();
    return m;
}

Mutex* at(u32 index) {
    u32 n = 0;
    for (u32 i = 0; i < MUTEX_MAX; i++) {
        if (!g_mutexes[i].used) continue;
        if (n == index) return &g_mutexes[i];
        n++;
    }
    return nullptr;
}

u32 count() { return g_mutex_count; }
const char* name(Mutex* m) { return m ? m->name : "?"; }

bool owned_by(Mutex* m, Thread* t) { return m && m->owner == t; }

// Called from thread::exit(): a terminated thread must never keep a mutex, or
// every other thread waiting on it would block forever.
void release_all(Thread* t) {
    if (!t) return;
    kernel_lock();
    for (u32 i = 0; i < MUTEX_MAX; i++) {
        Mutex* m = &g_mutexes[i];
        if (!m->used || m->owner != t) continue;

        m->recursion_depth = 0;
        Thread* next = dequeue_waiter(m);
        if (next) {
            m->owner = next;
            m->lock_count++;
            next->wait_mutex = nullptr;
            next->granted = m;
            next->state = ThreadState::READY;
            scheduler::add(next);
        } else {
            m->owner = nullptr;
        }
    }
    t->wait_mutex = nullptr;
    t->granted = nullptr;
    kernel_unlock();
}

bool try_lock(Mutex* m) {
    if (!m) return false;
    Thread* cur = thread::current();
    if (!cur) return false;
    kernel_lock();
    if (m->owner == nullptr) {
        m->owner = cur;
        m->lock_count++;
        kernel_unlock();
        return true;
    }
    if (m->owner == cur && m->recursive) {
        m->recursion_depth++;
        m->lock_count++;
        kernel_unlock();
        return true;
    }
    kernel_unlock();
    return false;
}

bool lock(Mutex* m) {
    if (!m) return false;
    Thread* cur = thread::current();
    if (!cur) return false;

    for (;;) {
        kernel_lock();
        // Ownership was handed to us directly by unlock(); just take it.
        if (cur->granted == m) {
            cur->granted = nullptr;
            kernel_unlock();
            return true;
        }
        if (m->owner == cur) {
            if (m->recursive) {
                m->recursion_depth++;
                m->lock_count++;
                kernel_unlock();
                return true;
            }
            kernel_unlock();
            return false;    // self-deadlock avoided
        }
        if (m->owner == nullptr) {
            m->owner = cur;
            m->lock_count++;
            cur->wait_mutex = nullptr;
            kernel_unlock();
            return true;
        }

        // Mutex is held: check the wait-for graph before blocking.
        if (would_deadlock(m, cur)) {
            kernel_unlock();
            return false;    // deadlock avoided, caller decides what to do
        }

        if (m->waiter_count < MUTEX_WAITERS_MAX) {
            enqueue_waiter(m, cur);
            cur->wait_mutex = m;
            m->block_count++;
            m->contention_count++;
            cur->state = ThreadState::WAITING;
            kernel_unlock();

            thread::yield();

            // Ownership is handed over directly in unlock(); loop and verify.
            continue;
        }
        kernel_unlock();
        thread::yield();    // wait queue full: back off and retry
    }
}

bool unlock(Mutex* m) {
    if (!m) return false;
    Thread* cur = thread::current();
    kernel_lock();

    if (m->owner != cur) {
        kernel_unlock();
        return false;        // not the owner
    }
    if (m->recursive && m->recursion_depth > 0) {
        m->recursion_depth--;
        kernel_unlock();
        return true;
    }

    Thread* next = dequeue_waiter(m);
    if (next) {
        m->owner = next;
        m->lock_count++;
        next->wait_mutex = nullptr;
        next->granted = m;
        next->state = ThreadState::READY;
        m->recursion_depth = 0;
        kernel_unlock();
        scheduler::add(next);    // direct handoff
        return true;
    }

    m->owner = nullptr;
    kernel_unlock();
    return true;
}

// Acquire two mutexes in a globally consistent order (address order).
bool lock_ordered(Mutex* a, Mutex* b) {
    if (!a || !b) return false;
    Mutex* first = ((u32)a < (u32)b) ? a : b;
    Mutex* second = ((u32)a < (u32)b) ? b : a;
    if (!lock(first)) return false;
    if (!lock(second)) {
        unlock(first);
        return false;
    }
    return true;
}

} // namespace mutex

namespace semaphore {

void init(Semaphore* s, const char* name, i32 initial) {
    if (!s) return;
    memset(s, 0, sizeof(Semaphore));
    s->count = initial;
    if (name) strncpy(s->name, name, sizeof(s->name) - 1);
}

static void enqueue(Semaphore* s, Thread* t) {
    if (s->waiter_count >= MUTEX_WAITERS_MAX) return;
    s->waiters[s->waiter_count++] = t;
}

void wait(Semaphore* s) {
    if (!s) return;
    Thread* cur = thread::current();
    for (;;) {
        kernel_lock();
        if (s->count > 0) {
            s->count--;
            kernel_unlock();
            return;
        }
        if (cur) {
            enqueue(s, cur);
            cur->state = ThreadState::WAITING;
        }
        kernel_unlock();
        thread::yield();
        if (!cur) return;
    }
}

bool signal(Semaphore* s) {
    if (!s) return false;
    kernel_lock();
    // Publish the permit first, then wake one waiter. The waiter re-checks the
    // count after it is scheduled, so the permit can not be lost if another
    // thread consumes it first.
    s->count++;
    if (s->waiter_count > 0) {
        Thread* t = s->waiters[0];
        for (u32 i = 1; i < s->waiter_count; i++) s->waiters[i - 1] = s->waiters[i];
        s->waiter_count--;
        s->waiters[s->waiter_count] = nullptr;
        t->state = ThreadState::READY;
        kernel_unlock();
        scheduler::add(t);
        return true;
    }
    kernel_unlock();
    return true;
}

} // namespace semaphore
