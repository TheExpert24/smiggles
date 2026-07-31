SUBDIR := myos
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
	$(MAKE) -C $(SUBDIR) all
	$(MAKE) -C $(SUBDIR) seed_bgf
	@cd tools && python3 seed_music.py ../$(SUBDIR)/hdd.img
	@sudo "$(QEMU)" \
		-drive file=$(SUBDIR)/smiggles.iso,media=cdrom,if=ide,bus=1,unit=0 \
		-drive file=$(SUBDIR)/hdd.img,format=raw,if=ide,bus=0,unit=0 \
		-boot d \
		-serial stdio \
		-netdev vmnet-bridged,id=net0,ifname=en0 \
		-device rtl8139,netdev=net0 \
		-audiodev coreaudio,id=snd0 \
		-device sb16,audiodev=snd0 \
		-machine pcspk-audiodev=snd0
clean:
	$(MAKE) -C $(SUBDIR) clean
	
