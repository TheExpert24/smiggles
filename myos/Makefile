ASM=nasm
CC=i686-elf-gcc
LD=i686-elf-ld
QEMU=qemu-system-i386

all: floppy.img

# Bootloader as RAW binary (512 bytes)
boot.bin: boot.asm
	$(ASM) -f bin boot.asm -o boot.bin

# Kernel as flat binary  
kernel.bin: kernel.c
	$(CC) -ffreestanding -c kernel.c -o kernel.o
	$(LD) -Ttext 0x1000 kernel.o -o kernel.bin --oformat binary

# Combine into floppy
floppy.img: boot.bin kernel.bin
	cat boot.bin kernel.bin > floppy.img
	truncate -s 1440k floppy.img

run: floppy.img
	$(QEMU) -fda floppy.img

clean:
	rm -f *.o *.bin *.img

.PHONY: all run clean
