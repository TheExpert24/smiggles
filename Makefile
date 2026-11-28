SUBDIR := myos
ifeq ($(OS),Windows_NT)
	QEMU := C:\Program Files\qemu\qemu-system-i386.exe
	AUDIODEV := dsound
	QEMU_RUN := "$(QEMU)"
	QEMU_DISPLAY := -display sdl
	NETDEV := -netdev user,id=net0
else
	QEMU := /usr/bin/qemu-system-i386
	AUDIODEV := pipewire
	QEMU_RUN := "$(QEMU)"
	QEMU_DISPLAY := -display sdl
	NETDEV := -netdev user,id=net0
endif

.PHONY: all run clean
all:
	$(MAKE) -C $(SUBDIR) all
usb-image:
	$(MAKE) -C $(SUBDIR) usb-image
run:
	$(MAKE) -C $(SUBDIR) all
	$(MAKE) -C $(SUBDIR) seed_bgf
	@cd tools && python3 seed_music.py ../$(SUBDIR)/hdd.img
	@$(QEMU_RUN) \
		$(QEMU_DISPLAY) \
		-drive file=$(SUBDIR)/smiggles.iso,media=cdrom,if=ide,bus=1,unit=0 \
		-drive file=$(SUBDIR)/hdd.img,format=raw,if=ide,bus=0,unit=0 \
		-boot d \
		-serial stdio \
		$(NETDEV) \
		-device rtl8139,netdev=net0 \
		-audiodev $(AUDIODEV),id=snd0 \
		-device sb16,audiodev=snd0 \
		-machine pcspk-audiodev=snd0
clean:
	$(MAKE) -C $(SUBDIR) clean
	
