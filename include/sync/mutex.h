// SharkXOS - mutex / semaphore style synchronisation
#pragma once
#include <kernel/common.h>
#include <process/thread.h>

#define MUTEX_MAX 16
#define MUTEX_WAITERS_MAX 8

struct Mutex {
    u32  id;
    char name[24];
    Thread* owner;
    Thread* waiters[MUTEX_WAITERS_MAX];
    u32  waiter_count;
    u32  lock_count;      // successful acquisitions
    u32  block_count;     // times a thread had to block
    u32  contention_count;
    bool used;
    bool recursive;
    u32  recursion_depth;
};

namespace mutex {
void   init();
Mutex* create(const char* name, bool recursive = false);
bool   lock(Mutex* m);
bool   lock_ordered(Mutex* a, Mutex* b);   // deadlock-free ordered acquisition
bool   try_lock(Mutex* m);
bool   unlock(Mutex* m);
bool   owned_by(Mutex* m, Thread* t);
void   release_all(Thread* t);             // drop every lock owned by a dying thread
Mutex* at(u32 index);
u32    count();
const char* name(Mutex* m);
} // namespace mutex

// Simple counting semaphore built on the scheduler wait queue
struct Semaphore {
    i32 count;
    Thread* waiters[MUTEX_WAITERS_MAX];
    u32 waiter_count;
    char name[24];
};

namespace semaphore {
void init(Semaphore* s, const char* name, i32 initial);
void wait(Semaphore* s);
bool signal(Semaphore* s);
} // namespace semaphore
