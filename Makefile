BUILD_DIR = build
ISO_DIR = $(BUILD_DIR)/iso
DISK_IMG = disk.img
HOSTPING_TAP = tap0
HOSTPING_HOST_CIDR = 192.168.76.1/24

.PHONY: all clean clean-disk setup-disk run run-nat run2 run-hostping run-hostping-debug hostping-up hostping-down k u

all: $(BUILD_DIR)/os.iso

k:
	$(MAKE) -C kernel

u:
	$(MAKE) -C userland

$(BUILD_DIR)/kernel.bin: k

$(BUILD_DIR)/user/shell.elf: u

$(DISK_IMG): u
	dd if=/dev/zero of=$(DISK_IMG) bs=1M count=32 2>/dev/null
	mkfs.fat -F 16 $(DISK_IMG) 2>/dev/null
	for elf in $(BUILD_DIR)/user/*.elf; do \
		[ -f "$$elf" ] || continue; \
		mcopy -i $(DISK_IMG) "$$elf" ::/"$$(basename "$$elf")" 2>/dev/null; \
	done
	mcopy -i $(DISK_IMG) assets/fonts/8x16.psf ::/FONT.PSF 2>/dev/null

$(BUILD_DIR)/os.iso: k u
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(BUILD_DIR)/kernel.bin $(ISO_DIR)/boot/
	cp $(BUILD_DIR)/user/shell.elf $(ISO_DIR)/boot/
	cp boot/grub/grub.cfg $(ISO_DIR)/boot/grub/
	grub-mkrescue -o $@ $(ISO_DIR) 2>/dev/null

clean:
	rm -rf $(BUILD_DIR) $(DISK_IMG)
	$(MAKE) -C kernel clean
	$(MAKE) -C userland clean

clean-disk:
	rm -f $(DISK_IMG)

setup-disk: $(DISK_IMG)

run: $(BUILD_DIR)/os.iso $(DISK_IMG)
	@echo "[run] TAP mode with host ping support"
	@./scripts/tap-up.sh $(HOSTPING_TAP) $(HOSTPING_HOST_CIDR)
	@echo "[run] Expected guest IP: 192.168.76.2"
	@echo "[run] Type directly in this terminal (serial stdio)."
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/os.iso -drive file=$(DISK_IMG),format=raw,if=ide -boot order=d -m 512M -vga std -serial stdio -netdev tap,id=hn0,ifname=$(HOSTPING_TAP),script=no,downscript=no -device rtl8139,netdev=hn0

run-nat: $(BUILD_DIR)/os.iso $(DISK_IMG)
	@echo "[run-nat] SLIRP/NAT mode (host -> guest ping is not expected)"
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/os.iso -drive file=$(DISK_IMG),format=raw,if=ide -boot order=d -m 512M -vga std -serial stdio -nic user,model=rtl8139

run2: $(BUILD_DIR)/os.iso $(DISK_IMG)
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/os.iso -drive file=$(DISK_IMG),format=raw,if=ide -boot order=d -m 256M -vga std -serial stdio -nic user,model=rtl8139 -d int,cpu_reset -D qemu.log

hostping-up:
	./scripts/tap-up.sh $(HOSTPING_TAP) $(HOSTPING_HOST_CIDR)

hostping-down:
	./scripts/tap-down.sh $(HOSTPING_TAP) $(HOSTPING_HOST_CIDR)

run-hostping: run

run-hostping-debug: $(BUILD_DIR)/os.iso $(DISK_IMG)
	@echo "[run-hostping-debug] Logging interrupts/resets to qemu.log"
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/os.iso -drive file=$(DISK_IMG),format=raw,if=ide -boot order=d -m 512M -vga std -serial stdio -netdev tap,id=hn0,ifname=$(HOSTPING_TAP),script=no,downscript=no -device rtl8139,netdev=hn0 -d int,cpu_reset -D qemu.log
