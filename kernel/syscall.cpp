// SharkXOS - system call interface (int 0x80)
#include <kernel/descriptors.h>
#include <kernel/panic.h>
#include <kernel/string.h>
#include <kernel/time.h>
#include <drivers/console.h>
#include <process/thread.h>
#include <security/access.h>

#define SYS_WRITE 1
#define SYS_EXIT  2
#define SYS_PUTC  3
#define SYS_TICKS 4
#define SYS_WHOAMI 5

extern "C" void user_exit_stub();

extern "C" Registers* syscall_dispatch(Registers* r) {
    switch (r->eax) {
    case SYS_WRITE: {
        const char* s = (const char*)r->ebx;
        u32 len = r->ecx;
        if (len > 1024) len = 1024;
        console::write(s, len);
        r->eax = len;
        break;
    }
    case SYS_PUTC:
        console::putc((char)r->ebx);
        r->eax = 0;
        break;
    case SYS_TICKS:
        r->eax = (u32)time::ticks();
        break;
    case SYS_WHOAMI:
        r->eax = (u32)access::current();
        break;
    case SYS_EXIT:
        kprintf("[ring3] user task exited with code %u\n", r->ebx);
        r->cs = KERNEL_CS;
        r->eip = (u32)user_exit_stub;
        r->eflags |= 0x200;
        r->eax = 0;
        break;
    default:
        r->eax = (u32)-1;
        break;
    }
    return r;
}
