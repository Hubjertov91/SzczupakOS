USER_ARCH_CC ?= gcc
USER_ARCH_LD ?= ld
USER_ARCH_AS ?= nasm

USER_ARCH_CFLAGS += -m64 -fno-pie -fno-stack-protector -fcf-protection=none -mcmodel=large
USER_ARCH_LDFLAGS +=
USER_ARCH_ASFLAGS += -f elf64

USER_CRT0_SRC ?= ../src/userland/arch/x86_64/crt0.S
