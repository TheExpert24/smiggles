#Note. Remember to copy the powershell before cloning this repo so you can comment out truncate and replace it with powershell.
ASM=nasm
CC=i686-elf-gcc
LD=i686-elf-ld
QEMU=qemu-system-i386

all: floppy.img hdd.img

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

basic.o: basic.c
	$(CC) -ffreestanding -c basic.c -o basic.o

editor.o: editor.c
	$(CC) -ffreestanding -c editor.c -o editor.o

commands.o: commands.c
	$(CC) -ffreestanding -c commands.c -o commands.o

diskio.o: diskio.c
	$(CC) -ffreestanding -c diskio.c -o diskio.o

ata.o: ata.c
	$(CC) -ffreestanding -c ata.c -o ata.o

syscall.o: syscall.c
	$(CC) -ffreestanding -c syscall.c -o syscall.o

protection.o: protection.c
	$(CC) -ffreestanding -c protection.c -o protection.o

kernel.bin: kernel_entry.o kernel.o interrupts.o memory.o filesystem.o display.o calculator.o basic.o editor.o commands.o diskio.o ata.o process.o syscall.o protection.o
	$(LD) -Ttext 0x10000 kernel_entry.o kernel.o interrupts.o memory.o filesystem.o display.o calculator.o basic.o editor.o commands.o diskio.o ata.o process.o syscall.o protection.o -o kernel.bin --oformat binary
process.o: process.c
	$(CC) -ffreestanding -c process.c -o process.o

floppy.img: boot.bin kernel.bin
	cat boot.bin kernel.bin > floppy.img
	@ truncate -s 1474560 floppy.img

hdd.img:
	@ if [ ! -f hdd.img ]; then qemu-img create -f raw hdd.img 10M; fi
run: floppy.img hdd.img
	$(QEMU) -fda floppy.img -hda hdd.img -serial stdio

clean:
	rm -f *.o *.bin *.img

.PHONY: all run clean
