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

clean:
	$(MAKE) -C $(SUBDIR) clean
