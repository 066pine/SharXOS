; SharkXOS - boot entry (Multiboot2, IA-32)
BITS 32

MB2_MAGIC equ 0xE85250D6

section .multiboot
align 8
multiboot_start:
    dd MB2_MAGIC
    dd 0                                    ; architecture: i386 protected mode
    dd multiboot_end - multiboot_start      ; header length
    dd -(MB2_MAGIC + 0 + (multiboot_end - multiboot_start))
    ; --- end tag ---
    align 8
    dw 0
    dw 0
    dd 8
multiboot_end:

section .bss
align 16
stack_bottom:
    resb 65536
stack_top:

section .text
global _start
extern kmain

_start:
    cli
    mov esp, stack_top
    xor ebp, ebp
    ; Preserve the multiboot handoff values (eax = magic, ebx = info pointer)
    push ebx
    push eax
    call kmain

.hang:
    cli
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
