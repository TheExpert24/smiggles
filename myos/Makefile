ASM=nasm
CC=i686-elf-gcc
LD=i686-elf-ld
QEMU=qemu-system-i386

all: floppy.img

# Bootloader as RAW binary (512 bytes)
boot.bin: boot.asm
	$(ASM) -f bin boot.asm -o boot.bin

kernel.bin: kernel.c
	$(CC) -ffreestanding -c kernel.c -o kernel.o
	$(LD) -Ttext 0x1000 kernel.o -o kernel.bin --oformat binary

# Kernel entry stub
kernel_entry.o: kernel_entry.asm
	nasm -f elf32 kernel_entry.asm -o kernel_entry.o

# Kernel as flat binary
kernel.bin: kernel.c kernel_entry.o
	$(CC) -ffreestanding -c kernel.c -o kernel.o
	$(LD) -Ttext 0x1000 kernel_entry.o kernel.o -o kernel.bin --oformat binary

# Combine into floppy
floppy.img: boot.bin kernel.bin
	cat boot.bin kernel.bin > floppy.img
	powershell -Command "if ((Get-Item 'floppy.img').length -lt 1474560) { [System.IO.File]::WriteAllBytes('floppy.img', ([System.IO.File]::ReadAllBytes('floppy.img') + ([byte[]](0..(1474559 - (Get-Item 'floppy.img').length) | ForEach-Object {0}))) ) }"

run: floppy.img
	$(QEMU) -fda floppy.img

clean:
	rm -f *.o *.bin *.img

.PHONY: all run clean
