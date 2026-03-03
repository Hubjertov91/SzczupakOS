BUILD_DIR = build
ISO_DIR = $(BUILD_DIR)/iso
DISK_IMG = disk.img
VDI_IMG = disk.vdi
ARCH ?= x86_64
PLATFORM ?= pc
VBOXMANAGE ?= VBoxManage
HOSTPING_TAP = tap0
HOSTPING_HOST_CIDR = 192.168.76.1/24
QEMU_USB_CONTROLLER = -device qemu-xhci,id=xhci
QEMU_USB_HID = -device usb-kbd,bus=xhci.0 -device usb-mouse,bus=xhci.0
QEMU_USB_HID_CONTROLLER = -device piix3-usb-uhci,id=uhci
QEMU_USB_HID_DEVICES = -device usb-kbd,bus=uhci.0,port=1 -device usb-mouse,bus=uhci.0,port=2
QEMU_GUI_CONSOLE = -vga std -serial stdio -monitor none
QEMU_HEADLESS_CONSOLE = -display none -serial stdio -monitor none

.PHONY: all clean clean-disk clean-vdi setup-disk setup-vdi matrix smoke regression run run-nat run2 run-hostping run-hostping-debug run-usb-hid run-nat-usb-hid run-serial run-nat-serial hostping-up hostping-down k u check-run-arch

all: $(BUILD_DIR)/os.iso $(DISK_IMG) $(VDI_IMG)

k:
	$(MAKE) -C kernel ARCH=$(ARCH) PLATFORM=$(PLATFORM)

u:
	$(MAKE) -C userland ARCH=$(ARCH)

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

$(VDI_IMG): $(DISK_IMG)
	@command -v $(VBOXMANAGE) >/dev/null 2>&1 || { \
		echo "[vdi] ERROR: $(VBOXMANAGE) not found in PATH"; \
		echo "[vdi] Install VirtualBox CLI or set VBOXMANAGE=/path/to/VBoxManage"; \
		exit 1; \
	}
	rm -f $(VDI_IMG)
	$(VBOXMANAGE) convertfromraw $(DISK_IMG) $(VDI_IMG) --format VDI >/dev/null

$(BUILD_DIR)/os.iso: k u
	@mkdir -p $(ISO_DIR)/boot/grub
	cp $(BUILD_DIR)/kernel.bin $(ISO_DIR)/boot/
	cp $(BUILD_DIR)/user/shell.elf $(ISO_DIR)/boot/SHELL.ELF
	cp assets/fonts/8x16.psf $(ISO_DIR)/boot/FONT.PSF
	cp boot/grub/grub.cfg $(ISO_DIR)/boot/grub/
	grub-mkrescue -o $@ $(ISO_DIR) 2>/dev/null

clean:
	rm -rf $(BUILD_DIR) $(DISK_IMG) $(VDI_IMG)
	$(MAKE) -C kernel ARCH=$(ARCH) PLATFORM=$(PLATFORM) clean
	$(MAKE) -C userland ARCH=$(ARCH) clean

clean-disk:
	rm -f $(DISK_IMG)

clean-vdi:
	rm -f $(VDI_IMG)

setup-disk: $(DISK_IMG)
setup-vdi: $(VDI_IMG)

matrix:
	./scripts/build-matrix.sh

smoke:
	./scripts/smoke-boot.sh

regression:
	./scripts/regression-smoke.sh

check-run-arch:
	@if [ "$(ARCH)" != "x86_64" ]; then \
		echo "[run] ERROR: run targets currently support only ARCH=x86_64"; \
		echo "[run] Requested ARCH=$(ARCH), PLATFORM=$(PLATFORM)"; \
		exit 1; \
	fi

run: check-run-arch $(BUILD_DIR)/os.iso $(DISK_IMG)
	@echo "[run] TAP mode with host ping support"
	@./scripts/tap-up.sh $(HOSTPING_TAP) $(HOSTPING_HOST_CIDR)
	@echo "[run] Expected guest IP: 192.168.76.2"
	@echo "[run] Type directly in this terminal (serial stdio)."
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/os.iso -drive file=$(DISK_IMG),format=raw,if=ide -boot order=d -m 512M $(QEMU_GUI_CONSOLE) -netdev tap,id=hn0,ifname=$(HOSTPING_TAP),script=no,downscript=no -device rtl8139,netdev=hn0 $(QEMU_USB_CONTROLLER)

run-nat: check-run-arch $(BUILD_DIR)/os.iso $(DISK_IMG)
	@echo "[run-nat] SLIRP/NAT mode (host -> guest ping is not expected)"
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/os.iso -drive file=$(DISK_IMG),format=raw,if=ide -boot order=d -m 512M $(QEMU_GUI_CONSOLE) -nic user,model=rtl8139 $(QEMU_USB_CONTROLLER)

run2: check-run-arch $(BUILD_DIR)/os.iso $(DISK_IMG)
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/os.iso -drive file=$(DISK_IMG),format=raw,if=ide -boot order=d -m 256M $(QEMU_GUI_CONSOLE) -nic user,model=rtl8139 $(QEMU_USB_CONTROLLER) -d int,cpu_reset -D qemu.log

hostping-up:
	@$(MAKE) check-run-arch ARCH=$(ARCH) PLATFORM=$(PLATFORM)
	./scripts/tap-up.sh $(HOSTPING_TAP) $(HOSTPING_HOST_CIDR)

hostping-down:
	@$(MAKE) check-run-arch ARCH=$(ARCH) PLATFORM=$(PLATFORM)
	./scripts/tap-down.sh $(HOSTPING_TAP) $(HOSTPING_HOST_CIDR)

run-hostping: run

run-hostping-debug: check-run-arch $(BUILD_DIR)/os.iso $(DISK_IMG)
	@echo "[run-hostping-debug] Logging interrupts/resets to qemu.log"
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/os.iso -drive file=$(DISK_IMG),format=raw,if=ide -boot order=d -m 512M $(QEMU_GUI_CONSOLE) -netdev tap,id=hn0,ifname=$(HOSTPING_TAP),script=no,downscript=no -device rtl8139,netdev=hn0 $(QEMU_USB_CONTROLLER) -d int,cpu_reset -D qemu.log

run-usb-hid: check-run-arch $(BUILD_DIR)/os.iso $(DISK_IMG)
	@echo "[run-usb-hid] TAP mode + USB keyboard/mouse attached"
	@./scripts/tap-up.sh $(HOSTPING_TAP) $(HOSTPING_HOST_CIDR)
	@echo "[run-usb-hid] Using UHCI USB HID path (usb-kbd + usb-mouse on uhci.0)"
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/os.iso -drive file=$(DISK_IMG),format=raw,if=ide -boot order=d -m 512M $(QEMU_GUI_CONSOLE) -netdev tap,id=hn0,ifname=$(HOSTPING_TAP),script=no,downscript=no -device rtl8139,netdev=hn0 $(QEMU_USB_HID_CONTROLLER) $(QEMU_USB_HID_DEVICES)

run-nat-usb-hid: check-run-arch $(BUILD_DIR)/os.iso $(DISK_IMG)
	@echo "[run-nat-usb-hid] SLIRP/NAT + USB keyboard/mouse attached"
	@echo "[run-nat-usb-hid] Using UHCI USB HID path (usb-kbd + usb-mouse on uhci.0)"
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/os.iso -drive file=$(DISK_IMG),format=raw,if=ide -boot order=d -m 512M $(QEMU_GUI_CONSOLE) -nic user,model=rtl8139 $(QEMU_USB_HID_CONTROLLER) $(QEMU_USB_HID_DEVICES)

run-serial: check-run-arch $(BUILD_DIR)/os.iso $(DISK_IMG)
	@echo "[run-serial] TAP mode headless serial console"
	@./scripts/tap-up.sh $(HOSTPING_TAP) $(HOSTPING_HOST_CIDR)
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/os.iso -drive file=$(DISK_IMG),format=raw,if=ide -boot order=d -m 512M $(QEMU_HEADLESS_CONSOLE) -netdev tap,id=hn0,ifname=$(HOSTPING_TAP),script=no,downscript=no -device rtl8139,netdev=hn0 $(QEMU_USB_CONTROLLER)

run-nat-serial: check-run-arch $(BUILD_DIR)/os.iso $(DISK_IMG)
	@echo "[run-nat-serial] NAT mode headless serial console"
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/os.iso -drive file=$(DISK_IMG),format=raw,if=ide -boot order=d -m 512M $(QEMU_HEADLESS_CONSOLE) -nic user,model=rtl8139 $(QEMU_USB_CONTROLLER)
