// SharkXOS - kernel entry point and boot sequence
#include <kernel/common.h>
#include <kernel/descriptors.h>
#include <kernel/multiboot2.h>
#include <kernel/time.h>
#include <kernel/shell.h>
#include <kernel/kernel.h>
#include <kernel/paging.h>
#include <kernel/string.h>
#include <drivers/console.h>
#include <drivers/vga.h>
#include <drivers/keyboard.h>
#include <drivers/serial.h>
#include <memory/heap.h>
#include <process/thread.h>
#include <process/scheduler.h>
#include <sync/mutex.h>
#include <fs/vfs.h>
#include <security/access.h>
#include <apps/apps.h>

// ---------------------------------------------------------------------------
static volatile u32 g_heartbeat = 0;
static volatile u32 g_memwatch = 0;
static volatile u32 g_idle_halts = 0;
static bool g_ready = false;
static u64 g_boot_tick = 0;

namespace kernel {
u32 heartbeat_count() { return g_heartbeat; }
u32 memwatch_samples() { return g_memwatch; }
const char* build_date() { return __DATE__; }
const char* build_time() { return __TIME__; }
const char* compiler() { return __VERSION__; }
u64 boot_tick() { return g_boot_tick; }
bool ready() { return g_ready; }
void set_ready(bool v) { g_ready = v; }
u32 idle_halts() { return g_idle_halts; }
void count_idle_halt() { g_idle_halts++; }
} // namespace kernel

// --------------------------------------------------------------- boot threads
static void heartbeat_entry(void*) {
    for (;;) {
        thread::sleep_ms(1000);
        g_heartbeat++;
        if ((g_heartbeat % 10) == 0) {
            klog("tick", "heartbeat thread alive, %u beats", g_heartbeat);
        }
    }
}

static void memwatch_entry(void*) {
    u32 peak = 0;
    for (;;) {
        thread::sleep_ms(3000);
        HeapStats s = heap::stats();
        if (s.used_bytes > peak) peak = s.used_bytes;
        g_memwatch++;
        (void)peak;
    }
}

// ---------------------------------------------------------------------- boot
static void print_logo() {
    console::set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console::write(
        "\n"
        "   _____ _               _    __  __\n"
        "  / ____| |             | |  |  \\/  |\n"
        " | (___ | |__   __ _ _ __| | _| \\  / |\n"
        "  \\___ \\| '_ \\ / _` | '__| |/ / |\\/| |\n"
        "  ____) | | | | (_| | |  |   <| |  | |\n"
        " |_____/|_| |_|\\__,_|_|  |_|\\_\\_|  |_|\n");
    console::set_color(VGA_WHITE, VGA_BLACK);
    console::write(
        "             SHARKXOS\n"
        "             Version " SHARKX_VERSION_STRING "\n"
        "             x86 32-BIT\n\n");
    console::reset_color();
}

static void boot_banner() {
    console::set_color(VGA_DARK_GREY, VGA_BLACK);
    console::write("SharkXOS " SHARKX_VERSION_STRING " - " SHARKX_ARCH_STRING
                   " - monolithic kernel\n");
    console::reset_color();
}

extern "C" void kmain(u32 magic, u32 info_addr) {
    console::init();
    boot_banner();

    multiboot::parse(magic, info_addr);
    const BootInfo& bi = multiboot::info();
    if (bi.valid) {
        klog("boot", "multiboot2 handoff ok (magic=0x%x, info=0x%x)",
             magic, info_addr);
        if (bi.bootloader[0]) klog("boot", "bootloader: %s", bi.bootloader);
        kprintf("       RAM: %u MB available\n",
                (unsigned)(bi.available_ram_bytes / (1024 * 1024)));
    } else {
        klog("boot", "warning: invalid multiboot2 information");
    }

    gdt::init();
    klog("gdt", "GDT/TSS loaded: kernel CS=0x8 DS=0x10, user CS=0x1b DS=0x23");

    idt::init();
    klog("idt", "IDT loaded (256 gates), PIC remapped to 0x20-0x2f");

    heap::init();
    klog("heap", "kernel heap ready: %u KiB", heap::capacity() / 1024);

    paging::init();

    access::init();
    vfs::init();
    klog("vfs", "ramdisk mounted: %u KiB, %u slots",
         VFS_RAMDISK_BYTES / 1024, VFS_MAX_FILES);

    mutex::init();
    keyboard::init();
    scheduler::init();
    scheduler::start();

    time::init();
    idt::enable_irq(IRQ_TIMER);
    idt::enable_irq(IRQ_KEYBOARD);
    idt::enable_irq(IRQ_SERIAL);
    idt::enable_irq(IRQ_CASCADE);
    serial::enable_rx_irq();

    g_boot_tick = time::ticks();

    if (!thread::create("heartbeat", heartbeat_entry, nullptr, 8 * 1024)) {
        klog("warn", "could not create heartbeat thread");
    }
    if (!thread::create("memwatch", memwatch_entry, nullptr, 8 * 1024)) {
        klog("warn", "could not create memwatch thread");
    }
    klog("sched", "round robin scheduler online, quantum %u ticks (%u ms)",
         scheduler::stats().quantum_ticks,
         1000u / time::hz() * scheduler::stats().quantum_ticks);

    kstd::sti();

    print_logo();
    console::set_color(VGA_DARK_GREY, VGA_BLACK);
    kprintf("Kernel initialisation complete. %u threads running.\n",
            thread::count());
    console::reset_color();

    g_ready = true;
    shell::init();
    shell::loop();
}
