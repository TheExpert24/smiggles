ASM=nasm
CC=i686-elf-gcc
LD=i686-elf-ld
QEMU=qemu-system-i386

all: floppy.img

boot.bin: boot.asm
	$(ASM) -f bin boot.asm -o boot.bin

kernel_entry.o: kernel_entry.asm
	$(ASM) -f elf32 kernel_entry.asm -o kernel_entry.o

kernel.o: kernel.c
	$(CC) -ffreestanding -c kernel.c -o kernel.o

kernel.bin: kernel_entry.o kernel.o
	$(LD) -Ttext 0x1000 kernel_entry.o kernel.o -o kernel.bin --oformat binary

floppy.img: boot.bin kernel.bin
	cat boot.bin kernel.bin > floppy.img
	@ truncate -s 1474560 floppy.img
run: floppy.img
	$(QEMU) -fda floppy.img

clean:
	rm -f *.o *.bin *.img

.PHONY: all run clean
