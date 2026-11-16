// SharkXOS - interactive shell
#include <kernel/shell.h>
#include <kernel/string.h>
#include <kernel/time.h>
#include <kernel/kernel.h>
#include <kernel/descriptors.h>
#include <drivers/console.h>
#include <drivers/keyboard.h>
#include <drivers/vga.h>
#include <process/thread.h>
#include <process/scheduler.h>
#include <apps/apps.h>
#include <memory/heap.h>

extern void register_system_commands();

namespace {

const ShellCommandEntry* g_commands[48];
u32 g_command_count = 0;

char g_line[SHELL_LINE_MAX];

int cmd_help(int, char**);

// --------------------------------------------------------------- core cmds
int cmd_clear(int, char**) {
    console::clear();
    return 0;
}

int cmd_about(int, char**) {
    console::set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console::write("SharkXOS " SHARKX_VERSION_STRING "\n");
    console::reset_color();
    kprintf("  Architecture : %s\n", SHARKX_ARCH_STRING);
    kprintf("  Kernel       : monolithic, preemptive round robin\n");
    kprintf("  Boot         : GRUB 2 / Multiboot2\n");
    kprintf("  Compiler     : %s\n", kernel::compiler());
    kprintf("  Build        : %s %s\n", kernel::build_date(), kernel::build_time());
    return 0;
}

int cmd_version(int, char**) {
    kprintf("SharkXOS version %s (%s)\n", SHARKX_VERSION_STRING, SHARKX_ARCH_STRING);
    kprintf("kernel build %s %s, gcc %s\n", kernel::build_date(), kernel::build_time(),
            kernel::compiler());
    return 0;
}

int cmd_uptime(int, char**) {
    u64 ms = time::uptime_ms();
    u64 secs = ms / 1000;
    kprintf("uptime: %llu ms (%llu.%03llu s), %llu ticks at %u Hz\n",
            (unsigned long long)ms,
            (unsigned long long)secs,
            (unsigned long long)(ms % 1000),
            (unsigned long long)time::ticks(), time::hz());
    return 0;
}

int cmd_logo(int, char**) {
    console::set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    console::write(
        "   _____ _               _    __  __\n"
        "  / ____| |             | |  |  \\/  |\n"
        " | (___ | |__   __ _ _ __| | _| \\  / |\n"
        "  \\___ \\| '_ \\ / _` | '__| |/ / |\\/| |\n"
        "  ____) | | | | (_| | |  |   <| |  | |\n"
        " |_____/|_| |_|\\__,_|_|  |_|\\_\\_|  |_|\n");
    console::reset_color();
    kprintf("             SHARKXOS\n             Version " SHARKX_VERSION_STRING
            "\n             x86 32-BIT\n");
    return 0;
}

int cmd_echo(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        console::write(argv[i]);
        if (i + 1 < argc) console::write(" ");
    }
    console::newline();
    return 0;
}

int cmd_yield(int, char**) {
    Thread* t = thread::current();
    kprintf("yielding thread %u (%s)...\n", t ? t->id : 0, t ? t->name : "?");
    thread::yield();
    kprintf("resumed thread %u (%s)\n", thread::current() ? thread::current()->id : 0,
            thread::current() ? thread::current()->name : "?");
    return 0;
}

static bool g_shutdown_requested = false;
static u32 g_reboot_requested = false;

int cmd_shutdown(int, char**) {
    kprintf("shutting down SharkXOS...\n");
    kprintf("(ACPI power off via QEMU)\n");
    g_shutdown_requested = true;
    return 0;
}

int cmd_reboot(int, char**) {
    kprintf("rebooting...\n");
    g_reboot_requested = true;
    return 0;
}

int cmd_diag(int, char**) {
    kprintf("SharkXOS diagnostics\n");
    kprintf("  VGA text buffer : 0x%x (%ux%u)\n", (unsigned)(u32)console::vga_buffer(),
            console::vga_width(), console::vga_height());

    // Write a marker cell, read it back from video memory.
    u32 x = console::vga_width() - 8;
    u32 y = console::vga_height() - 1;
    u16 old = console::read_cell(x, y);
    u16 marker = (u16)('S' | (0x4E << 8));
    console::write_cell(x, y, marker);
    u16 back = console::read_cell(x, y);
    console::write_cell(x, y, old);
    kprintf("  VGA read/write   : %s\n", (back == marker) ? "PASS" : "FAIL");

    u32 fl;
    __asm__ volatile("pushfl; popl %0" : "=r"(fl));
    kprintf("  interrupts (IF)  : %s\n", (fl & 0x200) ? "enabled" : "disabled");

    u64 t0 = time::ticks();
    time::busy_wait_ms(20);
    u64 t1 = time::ticks();
    kprintf("  PIT ticking      : %s (%llu ticks in ~20 ms)\n",
            (t1 > t0) ? "PASS" : "FAIL", (unsigned long long)(t1 - t0));

    kprintf("  Paging           : %s\n",
            (kstd::read_cr0() & 0x80000000u) ? "enabled" : "disabled (identity)");
    kprintf("  CPU              : %s\n", (fl & 0x200) ? "interrupts on" : "interrupts off");
    kprintf("  Threads          : %u\n", thread::count());
    kprintf("  Heap             : %u KiB total\n", heap::capacity() / 1024);
    return 0;
}

int cmd_help(int, char**) {
    kprintf("SharkXOS shell commands (%u registered):\n", g_command_count);
    for (u32 i = 0; i < g_command_count; i++) {
        console::set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        kprintf("  %-12s", g_commands[i]->name);
        console::reset_color();
        kprintf(" %s\n", g_commands[i]->help);
    }
    return 0;
}

const ShellCommandEntry CORE_COMMANDS[] = {
    {"help",     "list all commands",                    cmd_help},
    {"clear",    "clear the screen",                     cmd_clear},
    {"about",    "about SharkXOS",                       cmd_about},
    {"version",  "kernel version and build information",  cmd_version},
    {"uptime",   "time since boot",                      cmd_uptime},
    {"logo",     "print the SharkXOS boot logo",         cmd_logo},
    {"echo",     "echo the arguments back",              cmd_echo},
    {"yield",    "voluntarily give up the CPU",          cmd_yield},
    {"diag",     "run console / timer diagnostics",      cmd_diag},
    {"shutdown", "power off the machine (QEMU ACPI)",    cmd_shutdown},
    {"reboot",   "reset the machine",                    cmd_reboot},
    {"calc",     "scientific calculator (ui: calc ui)",  apps::calculator_main},
    {"editor",   "text editor",                          apps::editor_main},
    {"files",    "file manager",                         apps::filemanager_main},
    {"monitor",  "system monitor",                       apps::monitor_main},
};

void power_off() {
    // QEMU ACPI shutdown ports
    kstd::outw(0x604, 0x2000);
    kstd::outw(0xB004, 0x2000);
    kstd::outw(0x4004, 0x3400);
    // fall back to the keyboard controller reset line
    kstd::outb(0x64, 0xFE);
    for (;;) { kstd::cli(); kstd::hlt(); }
}

void do_reboot() {
    kstd::outb(0x64, 0xFE);
    for (volatile int i = 0; i < 1000000; i++) {}
    for (;;) { kstd::cli(); kstd::hlt(); }
}

} // namespace

namespace shell {

const char* prompt_string = "sharkx> ";

void init() {
    g_command_count = 0;
    for (u32 i = 0; i < sizeof(CORE_COMMANDS) / sizeof(CORE_COMMANDS[0]); i++) {
        register_command(CORE_COMMANDS[i]);
    }
    register_system_commands();
}

void register_command(const ShellCommandEntry& cmd) {
    if (g_command_count >= sizeof(g_commands) / sizeof(g_commands[0])) return;
    g_commands[g_command_count++] = &cmd;
}

void list_commands() { cmd_help(0, nullptr); }

void print_banner() {
    console::set_color(VGA_DARK_GREY, VGA_BLACK);
    kprintf("Type 'help' for the command list. Built-in apps: calc, editor, files, monitor.\n");
    console::reset_color();
}

void prompt() {
    console::set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    console::write(prompt_string);
    console::reset_color();
}

int arg_to_int(const char* s, int fallback) {
    bool ok = false;
    int v = kstd::to_int(s, &ok);
    return ok ? v : fallback;
}

void print_error(const char* msg) {
    console::set_color(VGA_LIGHT_RED, VGA_BLACK);
    kprintf("error: %s\n", msg);
    console::reset_color();
}

static int read_line(char* out, int max) {
    int len = 0;
    for (;;) {
        char c = keyboard::getchar();

        if (c == '\n') {
            console::putc('\n');
            out[len] = 0;
            return len;
        }
        if (c == '\b' || c == 127) {
            if (len > 0) {
                len--;
                console::backspace();
            }
            continue;
        }
        if (c == 3) {              // Ctrl-C
            console::write("^C\n");
            len = 0;
            out[0] = 0;
            return 0;
        }
        if (c == 12) {             // Ctrl-L
            console::clear();
            prompt();
            console::write(out);
            continue;
        }
        if (c < 32) continue;      // ignore other control characters
        if (len < max - 1) {
            out[len++] = c;
            console::putc(c);
        }
    }
}

bool execute_line(const char* line) {
    // copy and tokenise
    char buf[SHELL_LINE_MAX];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    kstd::trim(buf);
    if (buf[0] == 0) return true;

    char* argv[SHELL_ARGS_MAX];
    int argc = 0;
    char* p = buf;
    while (*p && argc < SHELL_ARGS_MAX) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = 0;
    }
    if (argc == 0) return true;

    for (u32 i = 0; i < g_command_count; i++) {
        if (strcmp(g_commands[i]->name, argv[0]) == 0) {
            int rc = g_commands[i]->handler(argc, argv);
            if (g_shutdown_requested) { power_off(); }
            if (g_reboot_requested) { do_reboot(); }
            return rc == 0;
        }
    }

    console::set_color(VGA_LIGHT_RED, VGA_BLACK);
    kprintf("unknown command: %s (try 'help')\n", argv[0]);
    console::reset_color();
    return true;
}

int loop() {
    print_banner();
    for (;;) {
        prompt();
        read_line(g_line, SHELL_LINE_MAX);
        execute_line(g_line);
    }
    return 0;
}

} // namespace shell
