// SharkXOS - Global Descriptor Table, TSS
#include <kernel/descriptors.h>
#include <kernel/string.h>
#include <drivers/console.h>

namespace {

GdtEntry g_gdt[6];
GdtPointer g_gdt_ptr;
TssEntry g_tss;

void set_gate_entry(int i, u32 base, u32 limit, u8 access, u8 gran) {
    g_gdt[i].base_low = (u16)(base & 0xFFFF);
    g_gdt[i].base_middle = (u8)((base >> 16) & 0xFF);
    g_gdt[i].base_high = (u8)((base >> 24) & 0xFF);
    g_gdt[i].limit_low = (u16)(limit & 0xFFFF);
    g_gdt[i].granularity = (u8)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    g_gdt[i].access = access;
}

} // namespace

namespace gdt {

void init() {
    memset(g_gdt, 0, sizeof(g_gdt));
    memset(&g_tss, 0, sizeof(g_tss));

    set_gate_entry(0, 0, 0, 0, 0);                         // null
    set_gate_entry(1, 0, 0xFFFFF, 0x9A, 0xCF);             // kernel code
    set_gate_entry(2, 0, 0xFFFFF, 0x92, 0xCF);             // kernel data
    set_gate_entry(3, 0, 0xFFFFF, 0xFA, 0xCF);             // user code  (DPL 3)
    set_gate_entry(4, 0, 0xFFFFF, 0xF2, 0xCF);             // user data  (DPL 3)

    g_tss.ss0 = KERNEL_DS;
    g_tss.esp0 = 0;
    g_tss.iomap_base = sizeof(TssEntry);
    set_gate_entry(5, (u32)&g_tss, sizeof(TssEntry) - 1, 0x89, 0x00);   // TSS (32-bit available)

    g_gdt_ptr.limit = (u16)(sizeof(g_gdt) - 1);
    g_gdt_ptr.base = (u32)&g_gdt;

    gdt_flush((u32)&g_gdt_ptr);
    tss_flush();
}

void set_kernel_stack(u32 esp0) {
    g_tss.esp0 = esp0;
}

u32 kernel_stack() { return g_tss.esp0; }

} // namespace gdt
