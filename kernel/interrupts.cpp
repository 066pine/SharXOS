// SharkXOS - central interrupt dispatcher
#include <kernel/descriptors.h>
#include <kernel/panic.h>
#include <kernel/time.h>
#include <drivers/console.h>
#include <drivers/keyboard.h>
#include <drivers/serial.h>
#include <process/scheduler.h>

extern "C" Registers* syscall_dispatch(Registers* r);
extern "C" void user_mode_abort(Registers* r);

namespace {

const char* exception_names[32] = {
    "Divide by zero", "Debug", "Non maskable interrupt", "Breakpoint",
    "Overflow", "Bound range exceeded", "Invalid opcode", "Device not available",
    "Double fault", "Coprocessor segment overrun", "Invalid TSS", "Segment not present",
    "Stack segment fault", "General protection fault", "Page fault", "Reserved",
    "x87 floating point", "Alignment check", "Machine check", "SIMD floating point",
    "Virtualisation", "Control protection", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor injection", "VMM communication", "Security", "Reserved"
};

void eoi(u8 irq) {
    if (irq >= 8) kstd::outb(0xA0, 0x20);
    kstd::outb(0x20, 0x20);
}

} // namespace

extern "C" Registers* interrupt_dispatch(Registers* r) {
    u32 no = r->int_no;

    if (no < 32) {
        // CPU exception
        if ((r->cs & 3) == 3) {
            // A ring 3 fault must not take down the kernel: report and abort it.
            user_mode_abort(r);
            return r;
        }
        char msg[96];
        ksnprintf(msg, sizeof(msg), "%s (vector %u)", exception_names[no], (unsigned)no);
        kernel_panic_regs(msg, r);
    }

    if (no == VEC_YIELD) {
        return scheduler::schedule(r);
    }

    if (no == VEC_SYSCALL) {
        return syscall_dispatch(r);
    }

    if (no >= 32 && no < 48) {
        u8 irq = (u8)(no - 32);
        eoi(irq);

        switch (irq) {
        case IRQ_TIMER:
            time::tick();
            return scheduler::tick(r);
        case IRQ_KEYBOARD:
            while (kstd::inb(0x64) & 0x01) keyboard::irq_handler();
            break;
        case IRQ_SERIAL:
            while (serial::rx_ready()) keyboard::push_serial_char(serial::read());
            break;
        default:
            break;   // spurious
        }
    }
    return r;
}
