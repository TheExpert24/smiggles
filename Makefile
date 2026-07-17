SUBDIR := myos

.PHONY: all run clean

all:
	$(MAKE) -C $(SUBDIR) all

run:
	@make -C $(SUBDIR) all
	@/opt/homebrew/bin/qemu-system-i386 \
		-drive file=$(SUBDIR)/smiggles.iso,media=cdrom,if=ide,bus=1,unit=0 \
		-drive file=$(SUBDIR)/hdd.img,format=raw,if=ide,bus=0,unit=0 \
		-boot d \
		-serial stdio \
		-net nic,model=rtl8139 \
		-net user \
		-audiodev coreaudio,id=snd0 \
		-machine pcspk-audiodev=snd0
aSUBDIR := myos

# Detect OS
ifeq ($(OS),Windows_NT)
	QEMU := C:\Program Files\qemu\qemu-system-i386.exe
	AUDIODEV := dsound
else
	QEMU := /opt/homebrew/bin/qemu-system-i386
	AUDIODEV := coreaudio
endif

.PHONY: all run clean

all:
	$(MAKE) -C $(SUBDIR) all

run:
	@$(MAKE) -C $(SUBDIR) all
	@"$(QEMU)" \
		-drive file=$(SUBDIR)/smiggles.iso,media=cdrom,if=ide,bus=1,unit=0 \
		-drive file=$(SUBDIR)/hdd.img,format=raw,if=ide,bus=0,unit=0 \
		-boot d \
		-serial stdio \
		-net nic,model=rtl8139 \
		-net user \
		-audiodev $(AUDIODEV),id=snd0 \
		-machine pcspk-audiodev=snd0

clean:
	$(MAKE) -C $(SUBDIR) clean
clean:
	$(MAKE) -C $(SUBDIR) clean
