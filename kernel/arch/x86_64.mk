ARCH_AS ?= nasm
ARCH_CC ?= gcc
ARCH_LD ?= ld

ARCH_ASFLAGS += -f elf64
ARCH_CFLAGS += -m64 -fno-pie -mcmodel=large
ARCH_LDFLAGS +=

ARCH_KERNEL_ASM_SRCS += ../src/kernel/arch/x86_64/boot.S \
                        ../src/kernel/arch/x86_64/interrupt.S \
                        ../src/kernel/arch/x86_64/entry.S \
                        ../src/kernel/arch/x86_64/gdt.S \
                        ../src/kernel/arch/x86_64/switch.S \
                        ../src/kernel/arch/x86_64/syscall.S

ARCH_KERNEL_C_SRCS += ../src/kernel/arch/x86_64/arch_api.c \
                      ../src/kernel/arch/x86_64/interrupt/idt.c \
                      ../src/kernel/arch/x86_64/cpu/gdt_init.c \
                      ../src/kernel/arch/x86_64/cpu/tss.c
