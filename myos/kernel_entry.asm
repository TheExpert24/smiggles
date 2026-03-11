[BITS 32]
[GLOBAL _start]
[GLOBAL exception_stub_table]
[EXTERN kernel_main]

[GLOBAL irq0_timer_handler]
[GLOBAL irq1_keyboard_handler]
[GLOBAL irq12_mouse_handler]
[GLOBAL isr_syscall_handler]
[GLOBAL load_idt]

[EXTERN timer_handler]
[EXTERN keyboard_handler]
[EXTERN mouse_handler]
[EXTERN exception_handler]
[EXTERN syscall_dispatch]

%macro EXC_NOERR 1
[GLOBAL isr%1]
isr%1:
    cli
    mov eax, [esp]
    mov ebx, [esp+4]
    mov ecx, [esp+8]
    push ecx
    push ebx
    push eax
    push dword 0
    push dword %1
    call exception_handler
.hang%1:
    cli
    hlt
    jmp .hang%1
%endmacro

%macro EXC_ERR 1
[GLOBAL isr%1]
isr%1:
    cli
    mov eax, [esp+4]
    mov ebx, [esp+8]
    mov ecx, [esp+12]
    mov edx, [esp]
    push ecx
    push ebx
    push eax
    push edx
    push dword %1
    call exception_handler
.hang%1:
    cli
    hlt
    jmp .hang%1
%endmacro

_start:
    mov esp, 0x90000
    call kernel_main
    cli
    hlt

; Timer IRQ0 handler stub
irq0_timer_handler:
    pusha
    call timer_handler
    popa
    iretd

; Keyboard IRQ1 handler stub
irq1_keyboard_handler:
    pusha
    call keyboard_handler
    popa
    iretd

; Mouse IRQ12 handler stub
irq12_mouse_handler:
    pusha
    call mouse_handler
    popa
    iretd

; Syscall interrupt handler (int 0x80)
; Input: eax = syscall number
; Output: eax = syscall return value
isr_syscall_handler:
    pusha
    mov eax, [esp + 28]
    mov ebx, [esp + 16]
    push ebx
    push eax
    call syscall_dispatch
    add esp, 8
    mov [esp + 28], eax
    popa
    iretd

; Load IDT routine
load_idt:
    mov eax, [esp+4] ; pointer to IDT ptr
    lidt [eax]
    ret

EXC_NOERR 0
EXC_NOERR 1
EXC_NOERR 2
EXC_NOERR 3
EXC_NOERR 4
EXC_NOERR 5
EXC_NOERR 6
EXC_NOERR 7
EXC_ERR   8
EXC_NOERR 9
EXC_ERR   10
EXC_ERR   11
EXC_ERR   12
EXC_ERR   13
EXC_ERR   14
EXC_NOERR 15
EXC_NOERR 16
EXC_ERR   17
EXC_NOERR 18
EXC_NOERR 19
EXC_NOERR 20
EXC_ERR   21
EXC_NOERR 22
EXC_NOERR 23
EXC_NOERR 24
EXC_NOERR 25
EXC_NOERR 26
EXC_NOERR 27
EXC_NOERR 28
EXC_ERR   29
EXC_ERR   30
EXC_NOERR 31

exception_stub_table:
    dd isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
    dd isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
    dd isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    dd isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
