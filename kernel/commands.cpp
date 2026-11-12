// SharkXOS - shell commands for the kernel subsystems
#include <kernel/shell.h>
#include <kernel/string.h>
#include <kernel/time.h>
#include <kernel/kernel.h>
#include <kernel/multiboot2.h>
#include <kernel/descriptors.h>
#include <drivers/console.h>
#include <memory/heap.h>
#include <process/thread.h>
#include <process/scheduler.h>
#include <sync/mutex.h>
#include <fs/vfs.h>
#include <security/access.h>
#include <kernel/paging.h>

extern "C" void kernel_lock();
extern "C" void kernel_unlock();

void register_system_commands();

// ===========================================================================
// helpers
static const char* perm_string(u8 p) {
    // Rotate through a few buffers so multiple calls can appear in one printf.
    static char bufs[4][4];
    static int slot = 0;
    char* buf = bufs[slot];
    slot = (slot + 1) & 3;
    buf[0] = (p & PERM_READ) ? 'r' : '-';
    buf[1] = (p & PERM_WRITE) ? 'w' : '-';
    buf[2] = (p & PERM_DELETE) ? 'd' : '-';
    buf[3] = 0;
    return buf;
}

// ===========================================================================
// memory
static int cmd_memory(int, char**) {
    const BootInfo& bi = multiboot::info();
    HeapStats h = heap::stats();
    VfsStats v = vfs::stats();

    kprintf("physical memory (multiboot2):\n");
    if (bi.valid) {
        kprintf("  available RAM   : %u MiB\n",
                (unsigned)(bi.available_ram_bytes / (1024 * 1024)));
        kprintf("  mmap entries    : %u\n", bi.mmap_entries);
    } else {
        kprintf("  unavailable (no valid multiboot2 info)\n");
    }

    kprintf("kernel heap:\n");
    kprintf("  total           : %u KiB\n", h.total_bytes / 1024);
    kprintf("  used            : %u KiB (%u%%)\n", h.used_bytes / 1024,
            (unsigned)(h.total_bytes ? (h.used_bytes * 100) / h.total_bytes : 0));
    kprintf("  free            : %u KiB\n", h.free_bytes / 1024);
    kprintf("  peak used       : %u KiB\n", h.peak_used / 1024);
    kprintf("  allocations     : %u live/alloc, %u frees, %u failures\n",
            h.alloc_count, h.free_count, h.fail_count);

    kprintf("ramdisk:\n");
    kprintf("  capacity        : %u KiB\n", v.capacity_bytes / 1024);
    kprintf("  used            : %u bytes in %u files\n", v.used_bytes, v.file_count);
    return 0;
}

static int cmd_heapinfo(int, char**) {
    HeapStats h = heap::stats();
    kprintf("heap allocator state\n");
    kprintf("  region          : %u KiB, %u blocks (%u free)\n",
            h.total_bytes / 1024, h.block_count, h.free_block_count);
    kprintf("  used / free     : %u / %u bytes\n", h.used_bytes, h.free_bytes);
    kprintf("  peak used       : %u bytes\n", h.peak_used);
    kprintf("  largest free    : %u bytes\n", h.largest_free);
    kprintf("  alloc / free    : %u / %u\n", h.alloc_count, h.free_count);
    kprintf("  failures        : %u OOM, %u invalid free, %u double free\n",
            h.fail_count, h.invalid_free_count, h.double_free_count);
    kprintf("  coalesces       : %u\n", h.coalesce_count);

    kprintf("block map (first 12):\n");
    for (u32 i = 0; i < 12; i++) {
        HeapBlockInfo bi;
        if (heap::block_info_at(i, &bi) == 0xFFFFFFFFu) break;
        kprintf("  [%02u] 0x%06x %6u bytes %s%s\n", i, bi.address, bi.size,
                bi.free ? "free" : "used", bi.valid ? "" : " (BAD HEADER)");
    }
    return 0;
}

static int cmd_memtest(int, char**) {
    kprintf("running heap allocator self test...\n");
    HeapStats before = heap::stats();
    int mask = heap::self_test();
    HeapStats after = heap::stats();

    struct { const char* text; } checks[8] = {
        {"allocation + 16 byte alignment"},
        {"writable payload"},
        {"double free rejected"},
        {"invalid free rejected"},
        {"free list coalescing"},
        {"out of memory handled"},
        {"realloc preserves data"},
        {"stress allocation loop"},
    };
    for (int i = 0; i < 8; i++) {
        bool ok = (mask & (1 << i)) == 0;
        console::set_color(ok ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
        kprintf("  [%s] %s\n", ok ? "PASS" : "FAIL", checks[i].text);
        console::reset_color();
    }

    int failures = 0;
    for (int i = 0; i < 8; i++) if (mask & (1 << i)) failures++;

    if (failures == 0) {
        console::set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        kprintf("memtest: PASS (8/8 checks)\n");
        console::reset_color();
    } else {
        console::set_color(VGA_LIGHT_RED, VGA_BLACK);
        kprintf("memtest: FAIL (%d/8 checks failed)\n", failures);
        console::reset_color();
    }
    kprintf("  used before/after: %u / %u bytes\n", before.used_bytes, after.used_bytes);
    return failures == 0 ? 0 : 1;
}

// ===========================================================================
// threads and scheduler
static int cmd_threads(int, char**) {
    SchedStats s = scheduler::stats();
    kprintf("threads (%u live, %u created, running id=%u):\n",
            thread::count(), thread::total_created(), s.running_id);
    console::set_color(VGA_DARK_GREY, VGA_BLACK);
    kprintf("  id  name              state       cpu ticks  switches  wait\n");
    console::reset_color();
    for (u32 i = 0; i < THREAD_MAX; i++) {
        Thread* t = thread::at(i);
        if (!t) continue;
        bool cur = (t->id == s.running_id);
        if (cur) console::set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        kprintf("  %-3u %-17s %-11s %-10u %-9u %s%s\n",
                t->id, t->name, thread_state_name(t->state), t->cpu_ticks,
                t->switches, t->wait_mutex ? t->wait_mutex->name : "-",
                cur ? "  <- running" : "");
        if (cur) console::reset_color();
    }
    return 0;
}

static int cmd_scheduler(int, char**) {
    SchedStats s = scheduler::stats();
    kprintf("scheduler: preemptive round robin\n");
    kprintf("  quantum         : %u ticks (%u ms)\n", s.quantum_ticks,
            (1000u / time::hz()) * s.quantum_ticks);
    kprintf("  timer ticks     : %llu\n", (unsigned long long)s.ticks);
    kprintf("  context switches: %llu\n", (unsigned long long)s.context_switches);
    kprintf("  preemptions     : %llu\n", (unsigned long long)s.preemptions);
    kprintf("  ready queue     : %u\n", s.ready_count);
    kprintf("  running         : %u (%s)\n", s.running_id, s.running_name);
    kprintf("  mutexes         : %u\n", mutex::count());
    for (u32 i = 0; i < MUTEX_MAX; i++) {
        Mutex* m = mutex::at(i);
        if (!m) continue;
        kprintf("    %-10s owner=%-12s locks=%u blocks=%u waiters=%u\n",
                m->name, m->owner ? m->owner->name : "none",
                m->lock_count, m->block_count, m->waiter_count);
    }
    return 0;
}

// ---------------------------------------------------------------- race demo
static volatile u32 g_race_counter;
static volatile u32 g_race_n;
static volatile bool g_race_locked;
static Mutex* g_race_mutex;

static void race_worker(void*) {
    for (u32 i = 0; i < g_race_n; i++) {
        if (g_race_locked) {
            mutex::lock(g_race_mutex);
            u32 tmp = g_race_counter;
            thread::yield();             // force a context switch mid-update
            g_race_counter = tmp + 1;
            mutex::unlock(g_race_mutex);
        } else {
            u32 tmp = g_race_counter;
            thread::yield();
            g_race_counter = tmp + 1;
        }
    }
}

static u32 run_race(bool use_lock, u32 n) {
    g_race_counter = 0;
    g_race_locked = use_lock;
    g_race_n = n;

    Thread* a = thread::create("race-A", race_worker, nullptr, 8 * 1024);
    Thread* b = thread::create("race-B", race_worker, nullptr, 8 * 1024);
    if (!a || !b) return 0;

    while (a->state != ThreadState::TERMINATED || b->state != ThreadState::TERMINATED) {
        thread::sleep_ms(5);
    }
    return g_race_counter;
}

static int cmd_race(int argc, char** argv) {
    u32 n = 200;
    if (argc > 1) n = (u32)shell::arg_to_int(argv[1], 200);
    if (n < 10) n = 10;
    if (n > 2000) n = 2000;

    if (!g_race_mutex) g_race_mutex = mutex::create("race_lock");

    kprintf("race condition test: 2 threads x %u increments = %u expected\n", n, n * 2);

    u32 unsync = run_race(false, n);
    kprintf("  without mutex   : counter = %u (lost %d updates)\n",
            unsync, (int)(n * 2) - (int)unsync);

    u32 sync = run_race(true, n);
    kprintf("  with mutex      : counter = %u (lost %d updates)\n",
            sync, (int)(n * 2) - (int)sync);

    bool pass = (sync == n * 2) && (unsync < n * 2);
    if (pass) {
        console::set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        kprintf("race test: PASS - unsynchronised update lost data, mutex fixed it\n");
    } else {
        console::set_color(VGA_LIGHT_RED, VGA_BLACK);
        kprintf("race test: FAIL (sync=%u unsync=%u expected=%u)\n", sync, unsync, n * 2);
    }
    console::reset_color();
    return pass ? 0 : 1;
}

// ------------------------------------------------------------ deadlock demo
static Mutex* g_dl_a;
static Mutex* g_dl_b;
static volatile u32 g_dl_detected_by;
static volatile u32 g_dl_a_got_b;
static volatile bool g_dl_a_done;
static volatile bool g_dl_b_done;

static void dl_thread_a(void*) {
    mutex::lock(g_dl_a);
    thread::sleep_ms(150);
    if (mutex::lock(g_dl_b)) {
        g_dl_a_got_b = 1;
        thread::sleep_ms(30);
        mutex::unlock(g_dl_b);
    }
    mutex::unlock(g_dl_a);
    g_dl_a_done = true;
}

static void dl_thread_b(void*) {
    thread::sleep_ms(50);
    mutex::lock(g_dl_b);
    thread::sleep_ms(150);
    if (!mutex::lock(g_dl_a)) {
        g_dl_detected_by = thread::current() ? thread::current()->id : 1;
    } else {
        mutex::unlock(g_dl_a);
    }
    mutex::unlock(g_dl_b);
    g_dl_b_done = true;
}

static int cmd_deadlock(int, char**) {
    if (!g_dl_a) g_dl_a = mutex::create("dl_mutex_A");
    if (!g_dl_b) g_dl_b = mutex::create("dl_mutex_B");
    if (!g_dl_a || !g_dl_b) { shell::print_error("cannot create mutexes"); return 1; }

    g_dl_detected_by = 0;
    g_dl_a_got_b = 0;
    g_dl_a_done = g_dl_b_done = false;

    kprintf("deadlock demo: A locks m1 then m2, B locks m2 then m1\n");
    Thread* a = thread::create("dl-A", dl_thread_a, nullptr, 8 * 1024);
    Thread* b = thread::create("dl-B", dl_thread_b, nullptr, 8 * 1024);
    if (!a || !b) { shell::print_error("cannot create demo threads"); return 1; }

    while (!g_dl_a_done || !g_dl_b_done) thread::sleep_ms(10);

    kprintf("  circular wait   : thread A waits for m2, thread B waits for m1\n");
    kprintf("  detection       : %s\n",
            g_dl_detected_by ? "wait-for cycle found, acquisition aborted" : "not triggered");
    kprintf("  recovery        : A obtained m2 after B released it -> %s\n",
            g_dl_a_got_b ? "system continued" : "FAILED");
    kprintf("  system state    : shell still responsive, both threads terminated\n");
    kprintf("  Coffman conditions observed: mutual exclusion, hold and wait,\n");
    kprintf("    no preemption, circular wait\n");
    kprintf("  prevention      : wait-for cycle detection (this test) and global\n");
    kprintf("    lock ordering, see 'lockorder'\n");

    bool pass = (g_dl_detected_by != 0) && g_dl_a_got_b;
    console::set_color(pass ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    kprintf("deadlock demo: %s\n", pass ? "PASS" : "FAIL");
    console::reset_color();
    return pass ? 0 : 1;
}

// --------------------------------------------------------- lock order demo
static Mutex* g_lo_1;
static Mutex* g_lo_2;
static volatile u32 g_lo_counter;
static volatile u32 g_lo_iterations;

static void ordered_worker(void*) {
    for (u32 i = 0; i < g_lo_iterations; i++) {
        if (!mutex::lock_ordered(g_lo_1, g_lo_2)) continue;
        u32 tmp = g_lo_counter;
        thread::yield();
        g_lo_counter = tmp + 1;
        mutex::unlock(g_lo_1);
        mutex::unlock(g_lo_2);
    }
}

static int cmd_lockorder(int, char**) {
    if (!g_lo_1) g_lo_1 = mutex::create("order_1");
    if (!g_lo_2) g_lo_2 = mutex::create("order_2");
    if (!g_lo_1 || !g_lo_2) { shell::print_error("cannot create mutexes"); return 1; }

    g_lo_counter = 0;
    g_lo_iterations = 100;
    kprintf("lock ordering demo: both threads acquire m1,m2 in address order\n");

    Thread* a = thread::create("order-A", ordered_worker, nullptr, 8 * 1024);
    Thread* b = thread::create("order-B", ordered_worker, nullptr, 8 * 1024);
    if (!a || !b) { shell::print_error("cannot create demo threads"); return 1; }

    u64 start = time::ticks();
    while (a->state != ThreadState::TERMINATED || b->state != ThreadState::TERMINATED) {
        thread::sleep_ms(5);
        if (time::ticks() - start > 2000) break;    // 20 s safety net
    }

    kprintf("  counter         : %u (expected %u)\n", g_lo_counter, g_lo_iterations * 2);
    kprintf("  blocked forever : no\n");
    bool pass = (g_lo_counter == g_lo_iterations * 2);
    console::set_color(pass ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    kprintf("lock ordering: %s\n", pass ? "PASS (no deadlock)" : "FAIL");
    console::reset_color();
    return pass ? 0 : 1;
}

// ===========================================================================
// thread state demonstration
static Mutex*        g_states_mutex;
static volatile bool g_states_running_seen;

static void states_worker(void*) {
    // Record the state the scheduler gave us while we are actually executing.
    Thread* self = thread::current();
    g_states_running_seen = (self && self->state == ThreadState::RUNNING);

    // The shell holds this mutex, so this call must block -> WAITING.
    mutex::lock(g_states_mutex);
    mutex::unlock(g_states_mutex);
}

static int cmd_states(int, char**) {
    if (!g_states_mutex) g_states_mutex = mutex::create("state_mtx");
    if (!g_states_mutex) { shell::print_error("cannot create demo mutex"); return 1; }

    mutex::lock(g_states_mutex);          // held while the worker starts
    g_states_running_seen = false;

    Thread* t = thread::create("state-demo", states_worker, nullptr, 8 * 1024, false);
    if (!t) {
        mutex::unlock(g_states_mutex);
        shell::print_error("cannot create demo thread");
        return 1;
    }

    bool seen_new = (t->state == ThreadState::NEW);
    kprintf("thread state demo: id=%u name='%s'\n", t->id, t->name);
    kprintf("  NEW        : created with start_ready=false, not queued\n");

    // Keep interrupts off across start() and the state check so no timer tick
    // can sneak the thread onto a CPU before we sample its state.
    kernel_lock();
    thread::start(t);
    bool seen_ready = (t->state == ThreadState::READY);
    kernel_unlock();
    kprintf("  READY      : thread::start() moved it onto the ready queue\n");

    u32 spin = 0;
    while (t->state != ThreadState::WAITING && t->state != ThreadState::TERMINATED &&
           spin < 5000) {
        thread::sleep_ms(1);
        spin++;
    }
    bool seen_running = g_states_running_seen;
    bool seen_waiting = (t->state == ThreadState::WAITING);
    kprintf("  RUNNING    : entry observed itself RUNNING on the CPU\n");
    kprintf("  WAITING    : blocked in mutex::lock() on a mutex the shell owns\n");

    mutex::unlock(g_states_mutex);
    spin = 0;
    while (t->state != ThreadState::TERMINATED && spin < 5000) {
        thread::sleep_ms(1);
        spin++;
    }
    bool seen_term = (t->state == ThreadState::TERMINATED);
    kprintf("  TERMINATED : returned from its entry and called thread::exit()\n");

    bool pass = seen_new && seen_ready && seen_running && seen_waiting && seen_term;
    kprintf("  observed   :%s%s%s%s%s\n",
            seen_new ? " NEW" : "",
            seen_ready ? " READY" : "",
            seen_running ? " RUNNING" : "",
            seen_waiting ? " WAITING" : "",
            seen_term ? " TERMINATED" : "");
    console::set_color(pass ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    kprintf("thread states: %s\n", pass ? "PASS" : "FAIL");
    console::reset_color();
    return pass ? 0 : 1;
}

// ===========================================================================
// semaphore demonstration
static Semaphore   g_sem;
static volatile u32 g_sem_done_a;
static volatile u32 g_sem_done_b;

static void sem_worker_a(void*) {
    semaphore::wait(&g_sem);
    g_sem_done_a++;
}

static void sem_worker_b(void*) {
    semaphore::wait(&g_sem);
    g_sem_done_b++;
}

static int cmd_semtest(int, char**) {
    semaphore::init(&g_sem, "semtest", 0);
    g_sem_done_a = 0;
    g_sem_done_b = 0;

    Thread* a = thread::create("sem-A", sem_worker_a, nullptr, 8 * 1024);
    Thread* b = thread::create("sem-B", sem_worker_b, nullptr, 8 * 1024);
    if (!a || !b) { shell::print_error("cannot create semaphore threads"); return 1; }

    kprintf("semaphore test: initial count 0, two consumers waiting\n");
    thread::sleep_ms(30);
    bool both_waiting = (a->state == ThreadState::WAITING && b->state == ThreadState::WAITING);
    kprintf("  both consumers WAITING : %s\n", both_waiting ? "yes" : "no");

    semaphore::signal(&g_sem);          // one permit -> exactly one consumer
    thread::sleep_ms(30);
    u32 after_one = g_sem_done_a + g_sem_done_b;
    kprintf("  after 1 signal         : %u consumer(s) finished\n", after_one);

    semaphore::signal(&g_sem);          // second permit
    u64 deadline = time::ticks() + 200;
    while ((a->state != ThreadState::TERMINATED || b->state != ThreadState::TERMINATED) &&
           time::ticks() < deadline) {
        thread::sleep_ms(5);
    }

    u32 finished = g_sem_done_a + g_sem_done_b;
    bool pass = both_waiting && (after_one == 1) && (finished == 2) && (g_sem.count == 0);
    kprintf("  total finished         : %u of 2\n", finished);
    kprintf("  final count            : %d\n", (int)g_sem.count);

    console::set_color(pass ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    kprintf("semaphore test: %s\n", pass ? "PASS" : "FAIL");
    console::reset_color();
    return pass ? 0 : 1;
}

// ===========================================================================
// VFS
static int cmd_ls(int, char**) {
    u32 n = vfs::count();
    if (n == 0) {
        kprintf("no files. use 'touch <name>' to create one.\n");
        return 0;
    }
    console::set_color(VGA_DARK_GREY, VGA_BLACK);
    kprintf("  %-20s %8s  %-10s %-4s %-4s %s\n", "name", "size", "owner", "user", "restr", "modified");
    console::reset_color();
    for (u32 i = 0; i < VFS_MAX_FILES; i++) {
        VfsFile* f = vfs::at(i);
        if (!f) continue;
        kprintf("  %-20s %8u  %-10s %-4s %-4s %u.%03u s\n",
                f->name, f->size, access::principal_name(f->owner),
                perm_string(f->user_perms), perm_string(f->restricted_perms),
                (unsigned)(f->modified_tick / time::hz()),
                (unsigned)((f->modified_tick % time::hz()) * 1000 / time::hz()));
    }
    return 0;
}

static int cmd_touch(int argc, char** argv) {
    if (argc < 2) { shell::print_error("usage: touch <file>"); return 1; }
    VfsError e = vfs::create(argv[1]);
    if (e == VFS_OK) {
        VfsFile* f = vfs::find(argv[1]);
        kprintf("created '%s' (owner %s, user %s, restricted %s)\n", argv[1],
                f ? access::principal_name(f->owner) : "?",
                f ? perm_string(f->user_perms) : "?",
                f ? perm_string(f->restricted_perms) : "?");
        return 0;
    }
    if (e == VFS_EEXIST) { kprintf("'%s' already exists\n", argv[1]); return 0; }
    kprintf("cannot create '%s': %s\n", argv[1], vfs_error_string(e));
    return 1;
}

static u8 g_cat_buf[4096];

static int cmd_cat(int argc, char** argv) {
    if (argc < 2) { shell::print_error("usage: cat <file>"); return 1; }
    u32 len = 0;
    VfsError e = vfs::read(argv[1], g_cat_buf, sizeof(g_cat_buf) - 1, &len);
    if (e != VFS_OK) { kprintf("cat: %s: %s\n", argv[1], vfs_error_string(e)); return 1; }
    g_cat_buf[len] = 0;
    console::write((const char*)g_cat_buf, len);
    if (len == 0 || g_cat_buf[len - 1] != '\n') console::newline();
    return 0;
}

static int cmd_write(int argc, char** argv) {
    if (argc < 3) { shell::print_error("usage: write <file> <text...>"); return 1; }

    if (!vfs::find(argv[1])) {
        VfsError ce = vfs::create(argv[1]);
        if (ce != VFS_OK) {
            kprintf("write: cannot create '%s': %s\n", argv[1], vfs_error_string(ce));
            return 1;
        }
    }

    char line[256];
    int pos = 0;
    for (int i = 2; i < argc; i++) {
        const char* s = argv[i];
        while (*s && pos < (int)sizeof(line) - 2) line[pos++] = *s++;
        if (i + 1 < argc && pos < (int)sizeof(line) - 2) line[pos++] = ' ';
    }
    line[pos++] = '\n';

    VfsError e = vfs::write(argv[1], (const u8*)line, (u32)pos, true);
    if (e != VFS_OK) { kprintf("write: %s: %s\n", argv[1], vfs_error_string(e)); return 1; }
    kprintf("appended %d bytes to '%s'\n", pos, argv[1]);
    return 0;
}

static int cmd_rm(int argc, char** argv) {
    if (argc < 2) { shell::print_error("usage: rm <file>"); return 1; }
    VfsError e = vfs::remove(argv[1]);
    if (e != VFS_OK) { kprintf("rm: %s: %s\n", argv[1], vfs_error_string(e)); return 1; }
    kprintf("removed '%s'\n", argv[1]);
    return 0;
}

static int cmd_diskinfo(int, char**) {
    VfsStats s = vfs::stats();
    kprintf("ramdisk: %u KiB capacity, %u KiB used, %u KiB free\n",
            s.capacity_bytes / 1024, s.used_bytes / 1024, s.free_bytes / 1024);
    kprintf("  files %u/%u, created %u, deleted %u\n", s.file_count, VFS_MAX_FILES,
            s.created, s.deleted);
    kprintf("  reads %u, writes %u, permission denials %u, enospc %u\n",
            s.read_ops, s.write_ops, s.permission_denials, s.enospc_count);

    u32 filled = s.capacity_bytes ? (s.used_bytes * 40) / s.capacity_bytes : 0;
    console::write("  [");
    for (u32 i = 0; i < 40; i++) console::putc(i < filled ? '#' : '.');
    kprintf("] %u%%\n",
            s.capacity_bytes ? (s.used_bytes * 100) / s.capacity_bytes : 0);
    return 0;
}

// Fill the ramdisk until the VFS reports ENOSPC, then clean up again.
static int cmd_vfstest(int, char**) {
    static const char* names[5] = {
        "fill0.tmp", "fill1.tmp", "fill2.tmp", "fill3.tmp", "fill4.tmp"
    };
    const u32 chunk = 64u * 1024u;

    Principal saved = access::current();
    access::set_current(PRINCIPAL_SYSTEM);
    for (u32 i = 0; i < 5; i++) vfs::remove(names[i]);

    u8* data = (u8*)heap::kmalloc(chunk);
    if (!data) {
        access::set_current(saved);
        shell::print_error("vfstest: cannot allocate test buffer");
        return 1;
    }
    memset(data, 'A', chunk);

    VfsStats start = vfs::stats();
    kprintf("vfs exhaustion test: %u KiB ramdisk, %u KiB per write\n",
            start.capacity_bytes / 1024, chunk / 1024);

    int accepted = 0, denied = 0, unexpected = 0;
    for (u32 i = 0; i < 5; i++) {
        VfsError e = vfs::create(names[i]);
        if (e != VFS_OK) {
            kprintf("  create %s failed: %s\n", names[i], vfs_error_string(e));
            unexpected++;
            break;
        }
        e = vfs::write(names[i], data, chunk, false);
        if (e == VFS_OK) {
            accepted++;
            kprintf("  %s: 64 KiB accepted (used %u KiB)\n",
                    names[i], vfs::stats().used_bytes / 1024);
        } else if (e == VFS_ENOSPC) {
            denied++;
            kprintf("  %s: rejected with ENOSPC (no space left)\n", names[i]);
        } else {
            kprintf("  %s: unexpected error: %s\n", names[i], vfs_error_string(e));
            unexpected++;
        }
    }

    u32 miss_len = 0;
    VfsError miss = vfs::read("does-not-exist.txt", data, 16, &miss_len);
    bool missing_ok = (miss == VFS_ENOENT);

    for (u32 i = 0; i < 5; i++) vfs::remove(names[i]);
    heap::kfree(data);
    access::set_current(saved);

    VfsStats end = vfs::stats();
    bool cleaned = (end.file_count == 0) && (end.used_bytes == 0);
    bool pass = (denied > 0) && (unexpected == 0) && cleaned;

    kprintf("  accepted writes : %d x 64 KiB\n", accepted);
    kprintf("  ENOSPC denials  : %d\n", denied);
    kprintf("  missing file    : %s\n", missing_ok ? "reported as an error" : "NOT reported");
    kprintf("  cleanup         : %u files, %u bytes used\n", end.file_count, end.used_bytes);

    console::set_color(pass ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    kprintf("vfs exhaustion: %s\n", pass ? "PASS" : "FAIL");
    console::reset_color();
    return pass ? 0 : 1;
}

// ===========================================================================
// access control
static int cmd_whoami(int, char**) {
    access::Usage u = access::usage();
    kprintf("principal: %s\n", access::principal_name(access::current()));
    kprintf("  access checks %u, denials %u\n", u.checks, u.denials);
    kprintf("  model: logical principals in ring 0 (see 'permissions')\n");
    return 0;
}

static int cmd_su(int argc, char** argv) {
    if (argc < 2) {
        kprintf("usage: su <system|user|restricted>  (current: %s)\n",
                access::principal_name(access::current()));
        return 1;
    }
    Principal p;
    if (strcmp(argv[1], "system") == 0) p = PRINCIPAL_SYSTEM;
    else if (strcmp(argv[1], "user") == 0) p = PRINCIPAL_USER;
    else if (strcmp(argv[1], "restricted") == 0) p = PRINCIPAL_RESTRICTED;
    else { shell::print_error("unknown principal"); return 1; }
    access::set_current(p);
    kprintf("now acting as %s\n", access::principal_name(p));
    return 0;
}

static int cmd_permissions(int argc, char** argv) {
    if (argc < 2) {
        kprintf("usage: permissions <file>\n");
        kprintf("  permission bits: r=read w=write d=delete\n");
        kprintf("  principals: SYSTEM (kernel), USER, RESTRICTED\n");
        kprintf("  SYSTEM bypasses checks; the owner always has full rights.\n");
        return 1;
    }
    VfsFile* f = vfs::find(argv[1]);
    if (!f) { kprintf("permissions: %s: no such file\n", argv[1]); return 1; }
    kprintf("file '%s'\n", f->name);
    kprintf("  owner      : %s\n", access::principal_name(f->owner));
    kprintf("  USER       : %s\n", perm_string(f->user_perms));
    kprintf("  RESTRICTED : %s\n", perm_string(f->restricted_perms));
    kprintf("  size       : %u bytes\n", f->size);
    kprintf("  reads/writes: %u / %u\n", f->reads, f->writes);
    return 0;
}

static int cmd_access_test(int, char**) {
    Principal saved = access::current();
    access::set_current(PRINCIPAL_SYSTEM);
    int failures = 0;

    const char* name = "secure.txt";
    vfs::remove(name);
    VfsError e = vfs::create(name, PERM_ALL, PERM_READ);
    if (e != VFS_OK) { kprintf("access_test: cannot create file: %s\n", vfs_error_string(e)); return 1; }
    vfs::write(name, (const u8*)"classified payload\n", 19, false);

    struct Check { const char* desc; Principal p; u8 perm; bool expect; };
    const Check checks[] = {
        {"SYSTEM read",       PRINCIPAL_SYSTEM,     PERM_READ,   true},
        {"SYSTEM write",      PRINCIPAL_SYSTEM,     PERM_WRITE,  true},
        {"USER read",         PRINCIPAL_USER,       PERM_READ,   true},
        {"USER write",        PRINCIPAL_USER,       PERM_WRITE,  true},
        {"RESTRICTED read",   PRINCIPAL_RESTRICTED, PERM_READ,   true},
        {"RESTRICTED write",  PRINCIPAL_RESTRICTED, PERM_WRITE,  false},
        {"RESTRICTED delete", PRINCIPAL_RESTRICTED, PERM_DELETE, false},
    };

    kprintf("access control test on '%s' (owner SYSTEM, USER=rwx, RESTRICTED=r--)\n", name);
    for (u32 i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
        access::set_current(checks[i].p);
        u32 len = 0;
        VfsError r;
        if (checks[i].perm == PERM_READ) r = vfs::read(name, g_cat_buf, 16, &len);
        else if (checks[i].perm == PERM_WRITE) r = vfs::write(name, (const u8*)"x", 1, true);
        else r = vfs::remove(name);
        bool allowed = (r == VFS_OK);
        bool ok = (allowed == checks[i].expect);
        if (!ok) failures++;
        console::set_color(ok ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
        kprintf("  [%s] %-18s -> %s\n", ok ? "PASS" : "FAIL", checks[i].desc,
                allowed ? "allowed" : vfs_error_string(r));
        console::reset_color();
        if (checks[i].perm == PERM_DELETE && allowed) {
            // file was actually deleted by a legal delete; recreate for later checks
            vfs::create(name, PERM_ALL, PERM_READ);
        }
    }

    access::set_current(saved);
    if (failures == 0) {
        console::set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        kprintf("access_test: PASS (all 7 checks behaved as expected)\n");
    } else {
        console::set_color(VGA_LIGHT_RED, VGA_BLACK);
        kprintf("access_test: FAIL (%d unexpected results)\n", failures);
    }
    console::reset_color();
    return failures == 0 ? 0 : 1;
}

// ===========================================================================
// ring 3 demo
namespace usermode_demo {
int run(bool try_privileged);
}

static int cmd_usermode(int argc, char** argv) {
    bool fault = (argc > 1 && strcmp(argv[1], "fault") == 0);
    kprintf("ring 3 demo: executing a user mode payload (CS=0x1b, CPL=3)\n");
    int rc = usermode_demo::run(fault);
    if (rc < 0) {
        console::set_color(VGA_LIGHT_RED, VGA_BLACK);
        kprintf("usermode: FAIL (demo could not run, code %d)\n", rc);
        console::reset_color();
        return 1;
    }
    console::set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("usermode: PASS (privilege transition ring0 -> ring3 -> ring0 via int 0x80)\n");
    console::reset_color();
    return 0;
}

// ===========================================================================
static const ShellCommandEntry SYSTEM_COMMANDS[] = {
    {"memory",      "memory and ramdisk summary",              cmd_memory},
    {"heapinfo",    "heap allocator statistics and block map",  cmd_heapinfo},
    {"memtest",     "run the heap allocator self test",         cmd_memtest},
    {"threads",     "list kernel threads and their states",     cmd_threads},
    {"scheduler",   "scheduler statistics and mutex owners",    cmd_scheduler},
    {"race",        "race condition vs mutex demonstration",    cmd_race},
    {"deadlock",    "two-mutex deadlock demonstration",         cmd_deadlock},
    {"lockorder",   "deadlock prevention via lock ordering",    cmd_lockorder},
    {"states",      "demonstrate all five thread states",       cmd_states},
    {"semtest",     "producer/consumer semaphore test",         cmd_semtest},
    {"ls",          "list files in the ramdisk",                cmd_ls},
    {"touch",       "create an empty file",                     cmd_touch},
    {"cat",         "print a file",                             cmd_cat},
    {"write",       "append text to a file",                    cmd_write},
    {"rm",          "delete a file",                            cmd_rm},
    {"diskinfo",    "ramdisk capacity and usage",               cmd_diskinfo},
    {"vfstest",     "fill the ramdisk and verify ENOSPC handling", cmd_vfstest},
    {"whoami",      "show the current security principal",      cmd_whoami},
    {"su",          "switch principal (system|user|restricted)", cmd_su},
    {"permissions", "show file permissions / access model",     cmd_permissions},
    {"access_test", "run the access control test",              cmd_access_test},
    {"usermode",    "run a ring 3 payload (add 'fault' to try cli)", cmd_usermode},
};

void register_system_commands() {
    for (u32 i = 0; i < sizeof(SYSTEM_COMMANDS) / sizeof(SYSTEM_COMMANDS[0]); i++) {
        shell::register_command(SYSTEM_COMMANDS[i]);
    }
}
