// SharkXOS - system monitor (all values come from live kernel structures)
#include <apps/apps.h>
#include <kernel/string.h>
#include <kernel/time.h>
#include <kernel/kernel.h>
#include <kernel/paging.h>
#include <drivers/console.h>
#include <drivers/keyboard.h>
#include <memory/heap.h>
#include <process/thread.h>
#include <process/scheduler.h>
#include <fs/vfs.h>
#include <security/access.h>

namespace {

void draw() {
    console::clear();

    console::set_color(VGA_BLACK, VGA_LIGHT_CYAN);
    kprintf(" SharkXOS system monitor                                         ");
    console::reset_color();
    console::newline();

    SchedStats s = scheduler::stats();
    HeapStats h = heap::stats();
    VfsStats v = vfs::stats();

    console::set_color(VGA_YELLOW, VGA_BLACK);
    kprintf("SYSTEM\n");
    console::reset_color();
    kprintf("  SharkXOS %s   %s\n", SHARKX_VERSION_STRING, SHARKX_ARCH_STRING);
    kprintf("  uptime %llu.%03llu s   kernel %s   paging %s (%u MiB identity)\n",
            (unsigned long long)(time::uptime_ms() / 1000),
            (unsigned long long)(time::uptime_ms() % 1000),
            kernel::ready() ? "ready" : "booting",
            paging::enabled() ? "on" : "off",
            paging::mapped_mb());

    console::set_color(VGA_YELLOW, VGA_BLACK);
    kprintf("SCHEDULER\n");
    console::reset_color();
    kprintf("  preemptive round robin, quantum %u ticks (%u ms)\n",
            s.quantum_ticks, (1000u / time::hz()) * s.quantum_ticks);
    kprintf("  ticks %llu   context switches %llu   preemptions %llu   ready %u\n",
            (unsigned long long)s.ticks, (unsigned long long)s.context_switches,
            (unsigned long long)s.preemptions, s.ready_count);
    kprintf("  running: %u (%s)   threads: %u   heartbeat: %u   memwatch: %u\n",
            s.running_id, s.running_name, thread::count(),
            kernel::heartbeat_count(), kernel::memwatch_samples());

    console::set_color(VGA_YELLOW, VGA_BLACK);
    kprintf("THREADS\n");
    console::reset_color();
    console::set_color(VGA_DARK_GREY, VGA_BLACK);
    kprintf("  id  name              state       cpu ticks  switches\n");
    console::reset_color();
    for (u32 i = 0; i < THREAD_MAX; i++) {
        Thread* t = thread::at(i);
        if (!t) continue;
        bool cur = (t->id == s.running_id);
        if (cur) console::set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        kprintf("  %-3u %-17s %-11s %-10u %u%s\n", t->id, t->name,
                thread_state_name(t->state), t->cpu_ticks, t->switches,
                cur ? "  <- running" : "");
        if (cur) console::reset_color();
    }

    console::set_color(VGA_YELLOW, VGA_BLACK);
    kprintf("MEMORY\n");
    console::reset_color();
    kprintf("  heap %u KiB: used %u KiB, free %u KiB, peak %u KiB\n",
            h.total_bytes / 1024, h.used_bytes / 1024, h.free_bytes / 1024,
            h.peak_used / 1024);
    kprintf("  allocations %u, frees %u, oom %u, double-free %u, invalid-free %u\n",
            h.alloc_count, h.free_count, h.fail_count, h.double_free_count,
            h.invalid_free_count);
    kprintf("  blocks %u (%u free), largest free %u bytes\n",
            h.block_count, h.free_block_count, h.largest_free);

    console::set_color(VGA_YELLOW, VGA_BLACK);
    kprintf("STORAGE / SECURITY\n");
    console::reset_color();
    kprintf("  ramdisk %u/%u KiB, %u files, %u reads, %u writes\n",
            v.used_bytes / 1024, v.capacity_bytes / 1024,
            v.file_count, v.read_ops, v.write_ops);
    kprintf("  principal %s, access denials %u\n",
            access::principal_name(access::current()), access::denials());

    console::set_color(VGA_DARK_GREY, VGA_BLACK);
    kprintf("\n  refreshing every second - press any key to return to the shell\n");
    console::reset_color();
}

} // namespace

namespace apps {

int monitor_main(int, char**) {
    for (;;) {
        draw();
        u64 start = time::ticks();
        bool quit = false;
        while (time::ticks() - start < time::hz()) {
            char c;
            if (keyboard::try_getchar(&c)) { quit = true; break; }
            thread::sleep_ms(50);
        }
        if (quit) break;
    }
    console::clear();
    kprintf("system monitor closed\n");
    return 0;
}

} // namespace apps
