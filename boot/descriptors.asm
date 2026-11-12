; SharkXOS - GDT / IDT / TSS low level helpers
BITS 32

section .text

global gdt_flush
gdt_flush:
    mov eax, [esp + 4]
    lgdt [eax]
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    jmp 0x08:.flush
.flush:
    ret

global tss_flush
tss_flush:
    mov ax, 0x28        ; TSS selector (index 5 * 8)
    ltr ax
    ret

global idt_load
idt_load:
    mov eax, [esp + 4]
    lidt [eax]
    ret

global gdt_get_cs
gdt_get_cs:
    xor eax, eax
    mov ax, cs
    ret

global gdt_get_ds
gdt_get_ds:
    xor eax, eax
    mov ax, ds
    ret

section .note.GNU-stack noalloc noexec nowrite progbits
