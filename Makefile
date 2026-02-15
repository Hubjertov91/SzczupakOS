AS = nasm
CC = gcc
LD = ld

ASFLAGS = -f elf64
CFLAGS = -m64 -ffreestanding -O0 -Wall -Wextra -nostdlib -nostdinc -fno-pie -mcmodel=large -I./include -I./include/kernel
LDFLAGS = -n -T kernel.ld

USER_CFLAGS = -m64 -ffreestanding -nostdlib -nostdinc -fno-pie -O0 -I./include/user
USER_LDFLAGS = -nostdlib -T src/userland/ld/user.ld

BUILD_DIR = build
OBJ_DIR = $(BUILD_DIR)/obj
ISO_DIR = $(BUILD_DIR)/iso
DISK_IMG = disk.img

KERNEL_OBJS = $(OBJ_DIR)/kernel/arch/x86_64/boot.o \
              $(OBJ_DIR)/kernel/arch/x86_64/interrupt_asm.o \
              $(OBJ_DIR)/kernel/arch/x86_64/entry.o \
              $(OBJ_DIR)/kernel/arch/x86_64/gdt_asm.o \
              $(OBJ_DIR)/kernel/arch/x86_64/switch.o \
              $(OBJ_DIR)/kernel/arch/x86_64/syscall_asm.o \
              $(OBJ_DIR)/kernel/kernel.o \
              $(OBJ_DIR)/kernel/drivers/vga.o \
              $(OBJ_DIR)/kernel/drivers/serial.o \
              $(OBJ_DIR)/kernel/drivers/pic.o \
              $(OBJ_DIR)/kernel/drivers/pit.o \
              $(OBJ_DIR)/kernel/drivers/keyboard.o \
              $(OBJ_DIR)/kernel/drivers/ata.o \
              $(OBJ_DIR)/kernel/drivers/framebuffer.o \
              $(OBJ_DIR)/kernel/drivers/psf.o \
              $(OBJ_DIR)/kernel/multiboot/multiboot2.o \
              $(OBJ_DIR)/kernel/mm/pmm.o \
              $(OBJ_DIR)/kernel/mm/heap.o \
              $(OBJ_DIR)/kernel/mm/vmm.o \
              $(OBJ_DIR)/kernel/mm/pagefault.o \
              $(OBJ_DIR)/kernel/mm/uaccess.o \
              $(OBJ_DIR)/kernel/fs/vfs.o \
              $(OBJ_DIR)/kernel/fs/tmpfs.o \
              $(OBJ_DIR)/kernel/fs/fat16.o \
              $(OBJ_DIR)/kernel/interrupt.o \
              $(OBJ_DIR)/kernel/task/task.o \
              $(OBJ_DIR)/kernel/task/scheduler.o \
              $(OBJ_DIR)/kernel/task/gdt.o \
              $(OBJ_DIR)/kernel/task/tss.o \
              $(OBJ_DIR)/kernel/task/syscall.o \
              $(OBJ_DIR)/kernel/loader/elf.o \
              $(OBJ_DIR)/kernel/terminal.o \
              $(OBJ_DIR)/kernel/string.o

USER_OBJS = $(OBJ_DIR)/user/crt0.o \
            $(OBJ_DIR)/user/math.o \
            $(OBJ_DIR)/user/shell.o \
            $(OBJ_DIR)/user/stdio.o \
            $(OBJ_DIR)/user/stdlib.o \
            $(OBJ_DIR)/user/string.o \
            $(OBJ_DIR)/user/syscall.o

all: $(BUILD_DIR)/os.iso

$(BUILD_DIR)/kernel.bin: $(KERNEL_OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $^

$(OBJ_DIR)/kernel/arch/x86_64/%.o: src/kernel/arch/x86_64/%.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(OBJ_DIR)/kernel/%.o: src/kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/drivers/%.o: src/kernel/drivers/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/fs/%.o: src/kernel/fs/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/mm/%.o: src/kernel/mm/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/task/%.o: src/kernel/task/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/loader/%.o: src/kernel/loader/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/user/%.o: src/userland/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(OBJ_DIR)/user/crt0.o: src/userland/arch/x86_64/crt0.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/user/shell.elf: $(USER_OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(USER_LDFLAGS) -o $@ $^

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

clean-disk:
	rm -f $(DISK_IMG)

setup-disk: $(DISK_IMG)

run: $(BUILD_DIR)/os.iso $(DISK_IMG)
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/os.iso -drive file=$(DISK_IMG),format=raw,if=ide -boot order=d -m 512M -serial stdio

run2: $(BUILD_DIR)/os.iso $(DISK_IMG)
	qemu-system-x86_64 -cdrom $(BUILD_DIR)/os.iso -drive file=$(DISK_IMG),format=raw,if=ide -boot order=d -m 256M -serial stdio -d int,cpu_reset -D qemu.log

.PHONY: all clean clean-disk setup-disk run run2