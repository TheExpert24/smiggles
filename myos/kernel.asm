BITS 16
ORG 0x7E00

start:
    mov si, msg
.print:
    lodsb
    or al, al
    jz hang
    mov ah, 0x0E
    int 0x10
    jmp .print

hang:
    cli
    hlt
    jmp hang

msg db "KERNEL RUNNING", 0
