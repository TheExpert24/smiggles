ASM = nasm
CC  = i686-elf-gcc
LD  = i686-elf-ld
CFLAGS  = -ffreestanding -m32 -O2 -Wall -Wextra -MMD -MP
LDFLAGS = -T linker.ld -nostdlib


# Tool discovery
# Checks Windows-specific paths and a local fallback for grub-mkrescue.
# Override any of these on the command line or in the environment if needed.
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

# Explicit availability checks — fail early with a clear diagnostic.
define check_tool
	@test -n "$(1)" || { echo "ERROR: $(2) not found. Install it or set $(3)= on the make command line."; exit 1; }
endef

# Host port-forwarding (override as needed, e.g. HOST_UDP_PORT=5555)
HOST_UDP_PORT  ?=
HOST_HTTP_PORT ?=

# Build the -net user hostfwd options only when the ports are set.
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

# Sources
C_SOURCES = \
	kernel.c interrupts.c memory.c filesystem.c display.c \
	calculator.c basic.c editor.c commands.c clipboard.c \
	diskio.c ata.c process.c syscall.c protection.c \
	pci.c rtl8139.c arp.c ipv4.c icmp.c udp.c net.c \
	tcp.c sockets.c hash.c logger.c

ASM_SOURCES = kernel_entry.asm

# Objects
OBJS = $(C_SOURCES:.c=.o) $(ASM_SOURCES:.asm=.o)

# Dependency files (.d)
DEPS = $(C_SOURCES:.c=.d)

all: smiggles.iso hdd.img

# Pattern rules
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm
	$(ASM) -f elf32 $< -o $@

# Link
kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@
	@if [ -n "$(GRUB_FILE)" ]; then \
		$(GRUB_FILE) --is-x86-multiboot2 $@ || { echo "Not multiboot2!"; exit 1; }; \
	fi

# GRUB config
iso/boot/grub/grub.cfg:
	mkdir -p iso/boot/grub
	printf 'set timeout=0\nset default=0\nterminal_output console\ninsmod multiboot2\nmultiboot2 /boot/kernel.elf\nboot\n' > $@

# ISO
smiggles.iso: kernel.elf iso/boot/grub/grub.cfg
	$(call check_tool,$(GRUB_MKRESCUE),grub-mkrescue / i686-elf-grub-mkrescue,GRUB_MKRESCUE)
	mkdir -p iso/boot
	cp kernel.elf iso/boot/kernel.elf
	$(GRUB_MKRESCUE) -o $@ iso >/dev/null

# HDD image--explicit device/bus options kept for workflows that need them.
# Override HDD_QEMU_OPTS on the command line if you need a custom drive config.
HDD_QEMU_OPTS ?= file=hdd.img,format=raw,if=ide,bus=0,unit=1

hdd.img:
	@if [ ! -f $@ ]; then qemu-img create -f raw $@ 10M; fi

# Run
run: smiggles.iso hdd.img
	$(call check_tool,$(QEMU),qemu-system-i386,QEMU)
	$(QEMU) \
		-drive file=smiggles.iso,media=cdrom,if=ide,bus=1,unit=0 \
		-drive $(HDD_QEMU_OPTS) \
		-boot d \
		-serial stdio \
		-net nic,model=rtl8139 \
		-net user$(HOSTFWD_UDP)$(HOSTFWD_HTTP)

# Clean
clean:
	rm -f *.o *.d *.elf *.iso *.img
	rm -rf iso

-include $(DEPS)
.PHONY: all run clean
