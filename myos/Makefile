ASM = nasm
CC = i686-elf-gcc
LD = i686-elf-ld
CFLAGS = -ffreestanding -m32 -O2 -Wall -Wextra -MMD -MP
LDFLAGS = -T linker.ld -nostdlib

QEMU ?= $(shell \
	command -v qemu-system-i386 2>/dev/null || \
	command -v qemu-system-i386.exe 2>/dev/null || \
	echo qemu-system-i386)

GRUB_MKRESCUE ?= $(shell \
	command -v grub-mkrescue 2>/dev/null || \
	command -v i686-elf-grub-mkrescue 2>/dev/null || \
	command -v grub2-mkrescue 2>/dev/null || \
	{ [ -x ./grub-mkrescue ] && echo ./grub-mkrescue; })

GRUB_FILE ?= $(shell \
	command -v grub-file 2>/dev/null || \
	command -v i686-elf-grub-file 2>/dev/null)

define check_tool
	@test -n "$(1)" || { echo "ERROR: $(2) not found. Install it or set $(3)= on the make command line."; exit 1; }
endef

HOST_UDP_PORT ?=
HOST_HTTP_PORT ?=

ifneq ($(HOST_UDP_PORT),)
	HOSTFWD_UDP := ,hostfwd=udp::$(HOST_UDP_PORT)-:$(HOST_UDP_PORT)
else
	HOSTFWD_UDP :=
endif

ifneq ($(HOST_HTTP_PORT),)
	HOSTFWD_HTTP := ,hostfwd=tcp::$(HOST_HTTP_PORT)-:80
else
	HOSTFWD_HTTP :=
endif

C_SOURCES = \
	kernel.c interrupts.c memory.c \
	filesystem_disk.c filesystem_inode.c filesystem_fileio.c filesystem_vfs.c filesystem_path.c strings.c \
	display.c calculator.c basic.c editor.c commands.c clipboard.c \
	diskio.c ata.c process.c syscall.c protection.c \
	elf_load.c \
	pci.c rtl8139.c arp.c ipv4.c icmp.c udp.c net.c \
	tcp.c sockets.c hash.c logger.c

ASM_SOURCES = kernel_entry.asm

OBJS = $(C_SOURCES:.c=.o) $(ASM_SOURCES:.asm=.o)
DEPS = $(C_SOURCES:.c=.d)

all: smiggles.iso hdd.img

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(ASM) -f elf32 $< -o $@

kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@
	@if [ -n "$(GRUB_FILE)" ]; then \
		$(GRUB_FILE) --is-x86-multiboot2 $@ || { echo "Not multiboot2!"; exit 1; }; \
	fi

iso/boot/grub/grub.cfg:
	mkdir -p iso/boot/grub
	printf 'set timeout=0\nset default=0\nterminal_output console\ninsmod multiboot2\nmultiboot2 /boot/kernel.elf\nboot\n' > $@

smiggles.iso: kernel.elf iso/boot/grub/grub.cfg
	$(call check_tool,$(GRUB_MKRESCUE),grub-mkrescue / i686-elf-grub-mkrescue,GRUB_MKRESCUE)
	mkdir -p iso/boot
	cp kernel.elf iso/boot/kernel.elf
	@if [ -d ../tools ] ; then cp -f ../tools/*.bgf iso/ 2>/dev/null || true ; fi
	$(GRUB_MKRESCUE) -o $@ iso >/dev/null

HDD_QEMU_OPTS ?= file=hdd.img,format=raw,if=ide,bus=0,unit=0

hdd.img:
	@if [ ! -f $@ ]; then qemu-img create -f raw $@ 10M; fi

seed_bgf: hdd.img
	@cd ../tools && python3 seed_badapple.py ../myos/hdd.img

run: smiggles.iso hdd.img seed_bgf
	@echo "Launcher redirected upstream."

clean:
	rm -f *.o *.d *.elf *.iso *.img
	rm -rf iso

-include $(DEPS)

.PHONY: all run clean seed_bgf
