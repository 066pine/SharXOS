// SharkXOS - Interrupt Descriptor Table and 8259 PIC
#include <kernel/descriptors.h>
#include <kernel/string.h>

namespace {

IdtEntry g_idt[256];
IdtPointer g_idt_ptr;

extern "C" {
void isr0();  void isr1();  void isr2();  void isr3();  void isr4();
void isr5();  void isr6();  void isr7();  void isr8();  void isr9();
void isr10(); void isr11(); void isr12(); void isr13(); void isr14();
void isr15(); void isr16(); void isr17(); void isr18(); void isr19();
void isr20(); void isr21(); void isr22(); void isr23(); void isr24();
void isr25(); void isr26(); void isr27(); void isr28(); void isr29();
void isr30(); void isr31();
void isr32(); void isr33(); void isr34(); void isr35(); void isr36();
void isr37(); void isr38(); void isr39(); void isr40(); void isr41();
void isr42(); void isr43(); void isr44(); void isr45(); void isr46();
void isr47();
void isr0x80();
void isr0x81();
}

typedef void (*Stub)();

Stub g_stubs[48] = {
    isr0,  isr1,  isr2,  isr3,  isr4,  isr5,  isr6,  isr7,
    isr8,  isr9,  isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23,
    isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
    isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39,
    isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47
};

void pic_remap() {
    u8 mask1 = kstd::inb(0x21);
    u8 mask2 = kstd::inb(0xA1);

    kstd::outb(0x20, 0x11); kstd::io_wait();
    kstd::outb(0xA0, 0x11); kstd::io_wait();
    kstd::outb(0x21, 0x20); kstd::io_wait();   // master offset 0x20
    kstd::outb(0xA1, 0x28); kstd::io_wait();   // slave offset 0x28
    kstd::outb(0x21, 0x04); kstd::io_wait();   // slave on IRQ2
    kstd::outb(0xA1, 0x02); kstd::io_wait();
    kstd::outb(0x21, 0x01); kstd::io_wait();
    kstd::outb(0xA1, 0x01); kstd::io_wait();

    kstd::outb(0x21, mask1);
    kstd::outb(0xA1, mask2);
}

} // namespace

namespace idt {

void set_gate(u8 num, u32 base, u16 sel, u8 flags) {
    g_idt[num].base_low = (u16)(base & 0xFFFF);
    g_idt[num].base_high = (u16)((base >> 16) & 0xFFFF);
    g_idt[num].sel = sel;
    g_idt[num].always0 = 0;
    g_idt[num].flags = flags;
}

void init() {
    memset(g_idt, 0, sizeof(g_idt));

    for (int i = 0; i < 48; i++) {
        set_gate((u8)i, (u32)g_stubs[i], KERNEL_CS, 0x8E);   // present, ring 0, 32-bit interrupt gate
    }
    set_gate(VEC_SYSCALL, (u32)isr0x80, KERNEL_CS, 0xEE);      // ring 3 callable
    set_gate(VEC_YIELD, (u32)isr0x81, KERNEL_CS, 0x8E);

    g_idt_ptr.limit = (u16)(sizeof(g_idt) - 1);
    g_idt_ptr.base = (u32)&g_idt;

    pic_remap();
    idt_load((u32)&g_idt_ptr);

    // mask everything, drivers unmask what they need
    kstd::outb(0x21, 0xFF);
    kstd::outb(0xA1, 0xFF);
}

void enable_irq(u8 irq) {
    u16 port = (irq < 8) ? 0x21 : 0xA1;
    u8 bit = (u8)(irq & 7);
    u8 mask = kstd::inb(port);
    mask = (u8)(mask & ~(1 << bit));
    kstd::outb(port, mask);
}

void disable_irq(u8 irq) {
    u16 port = (irq < 8) ? 0x21 : 0xA1;
    u8 bit = (u8)(irq & 7);
    u8 mask = kstd::inb(port);
    mask = (u8)(mask | (1 << bit));
    kstd::outb(port, mask);
}

} // namespace idt
