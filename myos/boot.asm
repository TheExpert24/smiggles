[BITS 16]
[ORG 0x7C00]

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    mov si, msg
.loop:
    lodsb
    or al, al
    jz load_kernel
    mov ah, 0x0E
    int 0x10
    jmp .loop

load_kernel:
    mov dl, 0x00
    mov ah, 0x00
    int 0x13
    
    ; Load the kernel starting at physical address 0x0010000.
    ; Use ES:BX = 0x1000:0x0000 => 0x0010000.
    mov ax, 0x1000
    mov es, ax

    ; Load kernel sectors one-by-one so we can safely cross tracks.
    ; Keep this comfortably above current kernel.bin size.
    mov si, 96          ; sectors to read
    mov ch, 0           ; cylinder
    mov dh, 0           ; head
    mov cl, 2           ; sector (starts after boot sector)
    mov bx, 0x0000      ; destination offset in ES

.read_next_sector:
    mov ah, 0x02
    mov al, 0x01
    int 0x13
    jc disk_error

    add bx, 512

    inc cl
    cmp cl, 19          ; sectors are 1..18
    jl .sector_ok
    mov cl, 1
    inc dh
    cmp dh, 2           ; heads 0..1
    jl .sector_ok
    mov dh, 0
    inc ch

.sector_ok:
    dec si
    jnz .read_next_sector

    lgdt [gdt_desc]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp 0x08:protected_mode

disk_error:
    mov si, err
.loop_err:
    lodsb
    or al, al
    jz .halt
    mov ah, 0x0E
    int 0x10
    jmp .loop_err
.halt:
    hlt

gdt_start:
    dq 0x0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_desc:
    dw 24
    dd gdt_start

[BITS 32]
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x90000
    ; Jump to kernel entry point at 0x0010000
    jmp 0x10000

msg db "Bootloader OK",13,10,0
err db "Disk error",13,10,0

times 510-($-$$) db 0
dw 0xAA55
