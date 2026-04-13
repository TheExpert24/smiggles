[BITS 32]
[GLOBAL _start]
[GLOBAL exception_stub_table]
[EXTERN kernel_main]
[EXTERN stack_top]

section .multiboot
align 8
mb2_header_start:
    dd 0xE85250D6                ; Multiboot2 magic
    dd 0                         ; i386 architecture
    dd mb2_header_end - mb2_header_start
    dd -(0xE85250D6 + 0 + (mb2_header_end - mb2_header_start))

    ; Required end tag.
    dw 0
    dw 0
    dd 8
mb2_header_end:

section .text

[GLOBAL irq0_timer_handler]
[GLOBAL irq1_keyboard_handler]
[GLOBAL irq12_mouse_handler]
[GLOBAL isr_syscall_handler]
[GLOBAL load_idt]

[EXTERN timer_handler]
[EXTERN keyboard_handler]
[EXTERN mouse_handler]
[EXTERN exception_handler]
[EXTERN handle_page_fault]
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
    cli
    mov esp, stack_top
    ; Multiboot2 hands us EAX=magic and EBX=multiboot info pointer.
    push ebx
    push eax
    call kernel_main
    add esp, 8
    cli
    hlt

; Timer IRQ0 handler stub
irq0_timer_handler:
    pusha
    push esp
    call timer_handler
    add esp, 4
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
; Linux ABI: eax=number, ebx=arg0, ecx=arg1, edx=arg2
; We also pass the saved CS to let syscall_dispatch detect ring-3 callers
; and route them to the Linux compatibility layer automatically.
;
; After pusha the stack layout is:
;   [esp+ 0]=edi [+4]=esi [+8]=ebp [+12]=old_esp [+16]=ebx
;   [+20]=edx   [+24]=ecx [+28]=eax
;   [+32]=eip   [+36]=cs  [+40]=eflags
;   (ring-3 callers also push esp3/ss3 above eflags)
isr_syscall_handler:
    pusha
    mov edi, esp             ; 6th arg: pointer to IRQContext-like frame
    mov eax, [esp + 28]      ; syscall number  (eax)
    mov ebx, [esp + 16]      ; arg0            (ebx)
    mov ecx, [esp + 24]      ; arg1            (ecx)
    mov edx, [esp + 20]      ; arg2            (edx)
    mov esi, [esp + 36]      ; saved cs  — used to detect ring-3 callers
    push edi
    push esi                 ; 5th arg: saved_cs
    push edx
    push ecx
    push ebx
    push eax
    call syscall_dispatch
    add esp, 24
    mov [esp + 28], eax      ; write return value back into saved eax
    popa
    iretd

; Load IDT routine
load_idt:
    mov eax, [esp+4] ; pointer to IDT ptr
    lidt [eax]
    ret

; ── Real cooperative context switch ────────────────────────────────────────
; void context_switch_asm(uint32_t *save_esp, uint32_t load_esp, uint32_t load_cr3)
; Saves edi/esi/ebx/ebp/eflags onto the current stack, records ESP in
; *save_esp, then loads load_esp and restores the new process's frame.
; A freshly created process has a pre-built frame (see process_create).
[GLOBAL context_switch_asm]
context_switch_asm:
    ; Stack at entry: [esp+0]=retaddr [esp+4]=save_esp* [esp+8]=load_esp [esp+12]=load_cr3
    pushfd
    push ebp
    push ebx
    push esi
    push edi
    ; Stack now:  [esp+0]=edi [esp+4]=esi [esp+8]=ebx [esp+12]=ebp
    ;             [esp+16]=eflags [esp+20]=retaddr
    ;             [esp+24]=save_esp* [esp+28]=load_esp [esp+32]=load_cr3
    mov eax, [esp+24]        ; eax = save_esp pointer
    mov [eax], esp           ; *save_esp = current kernel stack
    mov eax, [esp+32]
    mov cr3, eax             ; switch address space
    mov esp, [esp+28]        ; switch to new process kernel stack
    pop edi
    pop esi
    pop ebx
    pop ebp
    popfd
    ret                      ; resume target at its saved return address

; ── Ring-3 (user-mode) entry trampoline ─────────────────────────────────────
; void jump_to_ring3(uint32_t entry, uint32_t user_esp)
; Sets up an iretd frame and drops the CPU into CPL=3.
; GDT slots used: 0x18 user code | 0x20 user data  (see protection.c)
[GLOBAL jump_to_ring3]
jump_to_ring3:
    mov eax, [esp+4]         ; EIP for ring-3
    mov ecx, [esp+8]         ; ESP for ring-3
    ; Point data segments at user data descriptor with RPL=3 (0x20|3=0x23)
    mov dx, 0x23
    mov ds, dx
    mov es, dx
    mov fs, dx
    mov gs, dx
    ; iretd frame (pushed high→low): ss, esp, eflags, cs, eip
    push dword 0x23          ; ss  = user data | RPL 3
    push ecx                 ; user ESP
    pushfd
    or  dword [esp], 0x200   ; ensure IF=1 (interrupts enabled in user mode)
    push dword 0x1B          ; cs  = 0x18 user code | RPL 3
    push eax                 ; EIP = entry point
    iretd

; Restore user-visible register/iret state from IRQContext and return to ring 3.
; void resume_from_irq_context(IRQContext* ctx)
[GLOBAL resume_from_irq_context]
resume_from_irq_context:
    mov esi, [esp+4]         ; ctx pointer

    push dword [esi+52]      ; ss
    push dword [esi+48]      ; esp
    push dword [esi+40]      ; eflags
    push dword [esi+36]      ; cs
    push dword [esi+32]      ; eip

    mov ebp, [esi+8]
    mov ebx, [esi+16]
    mov edx, [esi+20]
    mov ecx, [esi+24]
    mov edi, [esi+0]
    mov eax, [esi+28]
    mov esi, [esi+4]
    iretd

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

[GLOBAL isr14]
isr14:
    cli
    pusha
    mov eax, cr2
    push eax
    push esp
    call handle_page_fault
    add esp, 8
    test eax, eax
    jnz .pf_handled

    mov eax, [esp+36]
    mov ebx, [esp+40]
    mov ecx, [esp+44]
    mov edx, [esp+32]
    push ecx
    push ebx
    push eax
    push edx
    push dword 14
    call exception_handler
.pf_hang:
    cli
    hlt
    jmp .pf_hang

.pf_handled:
    popa
    add esp, 4
    iretd

exception_stub_table:
    dd isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
    dd isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
    dd isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    dd isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
