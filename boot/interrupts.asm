; SharkXOS - interrupt service routine stubs (IA-32)
BITS 32

section .text
extern interrupt_dispatch
extern thread_trampoline

; ---------------------------------------------------------------- ISR stubs
%macro ISR_NOERR 1
global isr%1
isr%1:
    push dword 0
    push dword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr%1
isr%1:
    push dword %1
    jmp isr_common
%endmacro

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

ISR_NOERR 32
ISR_NOERR 33
ISR_NOERR 34
ISR_NOERR 35
ISR_NOERR 36
ISR_NOERR 37
ISR_NOERR 38
ISR_NOERR 39
ISR_NOERR 40
ISR_NOERR 41
ISR_NOERR 42
ISR_NOERR 43
ISR_NOERR 44
ISR_NOERR 45
ISR_NOERR 46
ISR_NOERR 47

ISR_NOERR 0x80
ISR_NOERR 0x81

; ------------------------------------------------------------ common handler
isr_common:
    pusha
    xor eax, eax
    mov ax, ds
    push eax

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push esp
    call interrupt_dispatch
    add esp, 4
    mov esp, eax            ; switch stacks if the scheduler returned a new frame

    pop eax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    popa
    add esp, 8              ; drop int_no and err_code
    iret

; ------------------------------------------------------- new thread bootstrap
global thread_entry_stub
thread_entry_stub:
    xor ebp, ebp
    and esp, 0xFFFFFFF0
    call thread_trampoline
.hang:
    cli
    hlt
    jmp .hang

; ------------------------------------------------- enter ring 3 (user mode)
; void enter_user_mode(u32 entry, u32 user_stack)
global enter_user_mode
enter_user_mode:
    push ebp
    mov ebp, esp
    cli
    mov ax, 0x23            ; user data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push dword 0x23         ; ss
    push dword [ebp + 12]   ; user esp
    pushfd
    pop eax
    or eax, 0x200           ; enable interrupts in user mode
    push eax                ; eflags
    push dword 0x1B         ; user code selector
    push dword [ebp + 8]    ; entry point
    iret

global user_exit_stub
extern user_mode_finished
user_exit_stub:
    xor ebp, ebp
    and esp, 0xFFFFFFF0
    call user_mode_finished
.hang:
    cli
    hlt
    jmp .hang

; ----------------------------------------------------- ring 3 payload entry
global user_payload_stub
extern user_payload_main
user_payload_stub:
    xor ebp, ebp
    and esp, 0xFFFFFFF0
    call user_payload_main
    mov eax, 2          ; SYS_EXIT
    xor ebx, ebx
    int 0x80
.hang:
    cli
    hlt
    jmp .hang

section .note.GNU-stack noalloc noexec nowrite progbits
