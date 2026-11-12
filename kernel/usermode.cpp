// SharkXOS - ring 3 demonstration payload and control
#include <kernel/common.h>
#include <kernel/descriptors.h>
#include <kernel/paging.h>
#include <kernel/string.h>
#include <drivers/console.h>
#include <memory/heap.h>
#include <process/thread.h>

extern "C" void enter_user_mode(u32 entry, u32 user_stack);
extern "C" void user_exit_stub();
extern "C" void user_payload_stub();

namespace {

volatile bool g_fault_mode = false;

static inline u32 syscall1(u32 n, u32 a) {
    u32 ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a) : "memory");
    return ret;
}

static inline u32 syscall2(u32 n, u32 a, u32 b) {
    u32 ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(n), "b"(a), "c"(b) : "memory");
    return ret;
}

static inline void sys_write(const char* s, u32 len) { syscall2(1, (u32)s, len); }
static inline void sys_exit(u32 code) { syscall1(2, code); for (;;) {} }

const char MSG_HELLO[] = "[ring3] hello from CPL=3 - this text was printed through int 0x80\n";
const char MSG_TICKS[] = "[ring3] SYS_TICKS returned a live value from the kernel\n";
const char MSG_NOTICKS[] = "[ring3] SYS_TICKS failed (returned 0)\n";
const char MSG_PRIV[] = "[ring3] about to execute cli - this must fault (privileged instruction)\n";
const char MSG_BAD[] = "[ring3] ERROR: cli did not fault\n";

static void user_task() {
    sys_write(MSG_HELLO, (u32)(sizeof(MSG_HELLO) - 1));
    if (g_fault_mode) {
        sys_write(MSG_PRIV, (u32)(sizeof(MSG_PRIV) - 1));
        __asm__ volatile("cli");
        sys_write(MSG_BAD, (u32)(sizeof(MSG_BAD) - 1));
        sys_exit(9);
    }
    u32 t = syscall1(4, 0);
    if (t != 0) sys_write(MSG_TICKS, (u32)(sizeof(MSG_TICKS) - 1));
    else sys_write(MSG_NOTICKS, (u32)(sizeof(MSG_NOTICKS) - 1));
    sys_exit(0);
}

void user_thread_entry(void*) {
    Thread* self = thread::current();
    if (self) gdt::set_kernel_stack(self->stack_base + self->stack_size);

    u8* stack = (u8*)heap::kmalloc(4096);
    if (!stack) { thread::exit(1); }

    // enter_user_mode never returns: the payload always exits via SYS_EXIT.
    enter_user_mode((u32)user_payload_stub, (u32)(stack + 4096 - 16));
    thread::exit(2);
}

} // namespace

// Called from the ring 0 stub after the ring 3 payload has exited.
extern "C" void user_mode_finished() {
    thread::exit(0);
}

// Called by the interrupt dispatcher when a fault happens in ring 3.  It
// rewrites the interrupt frame so that iret returns to ring 0 instead of
// panicking the whole kernel.
extern "C" void user_mode_abort(Registers* r) {
    kprintf("\n[ring3] FAULT in user mode: vector %u (err=0x%x, eip=0x%x)\n",
            r->int_no, r->err_code, r->eip);
    kprintf("[ring3] the privileged operation was blocked; kernel continues\n");
    r->cs = KERNEL_CS;
    r->eip = (u32)user_exit_stub;
    r->eflags |= 0x200;
}

// The payload entry called by user_payload_stub (in boot/interrupts.asm).
extern "C" void user_payload_main() {
    user_task();
    sys_exit(0);
}

namespace usermode_demo {

int run(bool try_privileged) {
    if (!paging::enabled()) return -1;
    g_fault_mode = try_privileged;

    Thread* t = thread::create("userdemo", user_thread_entry, nullptr, 16 * 1024);
    if (!t) return -2;

    u64 start = 0;
    (void)start;
    for (;;) {
        if (t->state == ThreadState::TERMINATED) break;
        thread::sleep_ms(5);
    }
    return 0;
}

} // namespace usermode_demo
