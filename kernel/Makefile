AS = nasm
CC = gcc
LD = ld

ASFLAGS = -f elf64
CFLAGS = -m64 -ffreestanding -O0 -Wall -Wextra -nostdlib -nostdinc -fno-pie -mcmodel=large -I../include -I../include/kernel
LDFLAGS = -n -T ../kernel.ld

BUILD_DIR = ../build
OBJ_DIR = $(BUILD_DIR)/obj

KERNEL_OBJS = $(OBJ_DIR)/kernel/arch/x86_64/boot.o \
              $(OBJ_DIR)/kernel/arch/x86_64/interrupt.o \
              $(OBJ_DIR)/kernel/arch/x86_64/entry.o \
              $(OBJ_DIR)/kernel/arch/x86_64/gdt.o \
              $(OBJ_DIR)/kernel/arch/x86_64/switch.o \
              $(OBJ_DIR)/kernel/arch/x86_64/syscall.o \
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
              $(OBJ_DIR)/kernel/net/net.o \
              $(OBJ_DIR)/kernel/loader/elf.o \
              $(OBJ_DIR)/kernel/terminal.o \
              $(OBJ_DIR)/kernel/string.o

all: $(BUILD_DIR)/kernel.bin

$(BUILD_DIR)/kernel.bin: $(KERNEL_OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $^

$(OBJ_DIR)/kernel/arch/x86_64/%.o: ../src/kernel/arch/x86_64/%.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(OBJ_DIR)/kernel/%.o: ../src/kernel/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/drivers/%.o: ../src/kernel/drivers/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/fs/%.o: ../src/kernel/fs/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/mm/%.o: ../src/kernel/mm/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/task/%.o: ../src/kernel/task/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/net/%.o: ../src/kernel/net/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/kernel/loader/%.o: ../src/kernel/loader/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR)/kernel.bin $(OBJ_DIR)/kernel

.PHONY: all clean
