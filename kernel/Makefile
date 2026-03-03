ARCH ?= x86_64
PLATFORM ?= pc

include arch/$(ARCH).mk
include platform/$(PLATFORM).mk

AS = $(ARCH_AS)
CC = $(ARCH_CC)
LD = $(ARCH_LD)

DEPFLAGS = -MMD -MP

BUILD_DIR ?= ../build
OBJ_DIR = $(BUILD_DIR)/obj

COMMON_CFLAGS = -ffreestanding -O0 -Wall -Wextra -nostdlib -nostdinc -I../include -I../include/kernel -mno-mmx -mno-sse -mno-sse2 -mno-red-zone
CFLAGS = $(COMMON_CFLAGS) $(ARCH_CFLAGS) $(PLATFORM_CFLAGS)
ASFLAGS = $(ARCH_ASFLAGS) $(PLATFORM_ASFLAGS)
LDFLAGS = -n -T ../kernel.ld $(ARCH_LDFLAGS) $(PLATFORM_LDFLAGS)

KERNEL_CORE_SRCS = ../src/kernel/kernel.c \
                   ../src/kernel/debug/panic.c \
                   ../src/kernel/pty.c \
                   ../src/kernel/terminal.c \
                   ../src/kernel/string.c

KERNEL_MM_SRCS = ../src/kernel/mm/pmm.c \
                 ../src/kernel/mm/heap.c \
                 ../src/kernel/mm/vmm.c \
                 ../src/kernel/mm/pagefault.c \
                 ../src/kernel/mm/uaccess.c

KERNEL_FS_SRCS = ../src/kernel/fs/vfs.c \
                 ../src/kernel/fs/tmpfs.c \
                 ../src/kernel/fs/fat16.c

KERNEL_TASK_SRCS = ../src/kernel/task/task.c \
                   ../src/kernel/task/scheduler.c \
                   ../src/kernel/task/syscall.c

KERNEL_NET_SRCS = ../src/kernel/net/net.c

KERNEL_MISC_SRCS = ../src/kernel/multiboot/multiboot2.c \
                   ../src/kernel/loader/elf.c

KERNEL_C_SRCS = $(KERNEL_CORE_SRCS) \
                $(KERNEL_MM_SRCS) \
                $(KERNEL_FS_SRCS) \
                $(KERNEL_TASK_SRCS) \
                $(KERNEL_NET_SRCS) \
                $(KERNEL_MISC_SRCS) \
                $(PLATFORM_DRIVER_SRCS) \
                $(ARCH_KERNEL_C_SRCS)

KERNEL_ASM_SRCS = $(ARCH_KERNEL_ASM_SRCS)

NET_SPLIT_SRCS = ../src/kernel/net/net_utils.c \
                 ../src/kernel/net/net_driver.c \
                 ../src/kernel/net/net_ip.c \
                 ../src/kernel/net/net_dhcp.c \
                 ../src/kernel/net/net_icmp.c \
                 ../src/kernel/net/net_dns.c \
                 ../src/kernel/net/net_tcp.c \
                 ../src/kernel/net/net_rx.c \
                 ../src/kernel/net/net_api.c

KERNEL_C_OBJS = $(patsubst ../src/%.c,$(OBJ_DIR)/%.o,$(KERNEL_C_SRCS))
KERNEL_ASM_OBJS = $(patsubst ../src/%.S,$(OBJ_DIR)/%.o,$(KERNEL_ASM_SRCS))
KERNEL_OBJS = $(KERNEL_C_OBJS) $(KERNEL_ASM_OBJS)

all: $(BUILD_DIR)/kernel.bin

$(BUILD_DIR)/kernel.bin: $(KERNEL_OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $^

$(OBJ_DIR)/%.o: ../src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: ../src/%.S
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(OBJ_DIR)/kernel/net/net.o: $(NET_SPLIT_SRCS)

clean:
	rm -rf $(BUILD_DIR)/kernel.bin $(OBJ_DIR)/kernel

.PHONY: all clean

-include $(KERNEL_C_OBJS:.o=.d)
