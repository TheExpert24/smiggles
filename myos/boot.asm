BITS 16
ORG 0x7C00

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov si, msg
.print:
    lodsb
    or al, al
    jz load_kernel
    mov ah, 0x0E
    int 0x10
    jmp .print

load_kernel:
    mov ah, 0x02
    mov al, 1
    mov ch, 0
    mov cl, 2
    mov dh, 0
    mov dl, 0x80
    mov bx, 0x7E00
    int 0x13

    jmp 0x0000:0x7E00

msg db "Bootloader OK ", 0

times 510-($-$$) db 0
dw 0xAA55
