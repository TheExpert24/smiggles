#Note. Remember to copy the powershell before cloning this repo so you can comment out truncate and replace it with powershell.
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

interrupts.o: interrupts.c
	$(CC) -ffreestanding -c interrupts.c -o interrupts.o

memory.o: memory.c
	$(CC) -ffreestanding -c memory.c -o memory.o

filesystem.o: filesystem.c
	$(CC) -ffreestanding -c filesystem.c -o filesystem.o

display.o: display.c
	$(CC) -ffreestanding -c display.c -o display.o

calculator.o: calculator.c
	$(CC) -ffreestanding -c calculator.c -o calculator.o

editor.o: editor.c
	$(CC) -ffreestanding -c editor.c -o editor.o

commands.o: commands.c
	$(CC) -ffreestanding -c commands.c -o commands.o

kernel.bin: kernel_entry.o kernel.o interrupts.o memory.o filesystem.o display.o calculator.o editor.o commands.o
	$(LD) -Ttext 0x1000 kernel_entry.o kernel.o interrupts.o memory.o filesystem.o display.o calculator.o editor.o commands.o -o kernel.bin --oformat binary

floppy.img: boot.bin kernel.bin
	cat boot.bin kernel.bin > floppy.img
	@ truncate -s 1474560 floppy.img
run: floppy.img
	$(QEMU) -fda floppy.img

clean:
	rm -f *.o *.bin *.img

.PHONY: all run clean
