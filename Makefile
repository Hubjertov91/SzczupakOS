BUILD_DIR = build
ISO_DIR = $(BUILD_DIR)/iso
DISK_IMG = disk.img

all: $(BUILD_DIR)/os.iso

k: $(BUILD_DIR)/kernel.bin

u: $(BUILD_DIR)/user/shell.elf

$(BUILD_DIR)/kernel.bin:
	$(MAKE) -C kernel

$(BUILD_DIR)/user/shell.elf:
	$(MAKE) -C userland

$(DISK_IMG): $(BUILD_DIR)/user/shell.elf
	dd if=/dev/zero of=$(DISK_IMG) bs=1M count=32 2>/dev/null
	mkfs.fat -F 16 $(DISK_IMG) 2>/dev/null
	mcopy -i $(DISK_IMG) $(BUILD_DIR)/user/shell.elf ::/SHELL.ELF 2>/dev/null
	mcopy -i $(DISK_IMG) assets/fonts/8x16.psf ::/FONT.PSF 2>/dev/null

$(BUILD_DIR)/os.iso: $(BUILD_DIR)/kernel.bin $(BUILD_DIR)/user/shell.elf
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
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/os.iso -drive file=$(DISK_IMG),format=raw,if=ide -boot order=d -m 512M -serial stdio

run2: $(BUILD_DIR)/os.iso $(DISK_IMG)
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/os.iso -drive file=$(DISK_IMG),format=raw,if=ide -boot order=d -m 256M -serial stdio -d int,cpu_reset -D qemu.log

.PHONY: all clean clean-disk setup-disk run run2 k u