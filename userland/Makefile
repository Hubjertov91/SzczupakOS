ARCH ?= x86_64

include arch/$(ARCH).mk

CC = $(USER_ARCH_CC)
LD = $(USER_ARCH_LD)
AS = $(USER_ARCH_AS)

USER_CFLAGS = -ffreestanding -nostdlib -nostdinc -O0 -I../include/user $(USER_ARCH_CFLAGS)
USER_LDFLAGS = -nostdlib -T ../src/userland/ld/user.ld $(USER_ARCH_LDFLAGS)
DEPFLAGS = -MMD -MP

BUILD_DIR = ../build
OBJ_DIR = $(BUILD_DIR)/obj

PROGRAM_SRCS = $(wildcard ../src/userland/programs/*.c)
PROGRAMS = $(patsubst ../src/userland/programs/%.c,%,$(PROGRAM_SRCS))
USER_ELFS = $(addprefix $(BUILD_DIR)/user/,$(addsuffix .elf,$(PROGRAMS)))
PROGRAM_OBJS = $(addprefix $(OBJ_DIR)/user/programs/,$(addsuffix .o,$(PROGRAMS)))
DESKTOP_SRCS = $(wildcard ../src/userland/programs/desktop/*.c)
DESKTOP_OBJS = $(patsubst ../src/userland/%.c,$(OBJ_DIR)/user/%.o,$(DESKTOP_SRCS))

USER_COMMON_OBJS = $(OBJ_DIR)/user/crt0.o \
                   $(OBJ_DIR)/user/lib/gui.o \
                   $(OBJ_DIR)/user/lib/math.o \
                   $(OBJ_DIR)/user/lib/netcli.o \
                   $(OBJ_DIR)/user/lib/stdio.o \
                   $(OBJ_DIR)/user/lib/stdlib.o \
                   $(OBJ_DIR)/user/lib/string.o \
                   $(OBJ_DIR)/user/syscall.o

all: $(USER_ELFS)

$(BUILD_DIR)/user/desktop.elf: $(USER_COMMON_OBJS) $(OBJ_DIR)/user/programs/desktop.o $(DESKTOP_OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(USER_LDFLAGS) -o $@ $^

$(BUILD_DIR)/user/%.elf: $(USER_COMMON_OBJS) $(OBJ_DIR)/user/programs/%.o
	@mkdir -p $(dir $@)
	$(LD) $(USER_LDFLAGS) -o $@ $^

$(OBJ_DIR)/user/%.o: ../src/userland/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) $(DEPFLAGS) -c $< -o $@

$(OBJ_DIR)/user/crt0.o: $(USER_CRT0_SRC)
	@mkdir -p $(dir $@)
	$(AS) $(USER_ARCH_ASFLAGS) $< -o $@

clean:
	rm -rf $(BUILD_DIR)/user $(OBJ_DIR)/user

.SECONDARY: $(PROGRAM_OBJS) $(DESKTOP_OBJS)
.PHONY: all clean

-include $(USER_COMMON_OBJS:.o=.d) $(PROGRAM_OBJS:.o=.d) $(DESKTOP_OBJS:.o=.d)
