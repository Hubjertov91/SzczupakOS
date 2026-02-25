CC = gcc
LD = ld

USER_CFLAGS = -m64 -ffreestanding -nostdlib -nostdinc -fno-pie -fno-stack-protector -fcf-protection=none -mcmodel=large -O0 -I../include/user
USER_LDFLAGS = -nostdlib -T ../src/userland/ld/user.ld

BUILD_DIR = ../build
OBJ_DIR = $(BUILD_DIR)/obj

PROGRAM_SRCS = $(wildcard ../src/userland/programs/*.c)
PROGRAMS = $(patsubst ../src/userland/programs/%.c,%,$(PROGRAM_SRCS))
USER_ELFS = $(addprefix $(BUILD_DIR)/user/,$(addsuffix .elf,$(PROGRAMS)))
PROGRAM_OBJS = $(addprefix $(OBJ_DIR)/user/programs/,$(addsuffix .o,$(PROGRAMS)))

USER_COMMON_OBJS = $(OBJ_DIR)/user/crt0.o \
                   $(OBJ_DIR)/user/lib/gui.o \
                   $(OBJ_DIR)/user/lib/math.o \
                   $(OBJ_DIR)/user/lib/netcli.o \
                   $(OBJ_DIR)/user/lib/stdio.o \
                   $(OBJ_DIR)/user/lib/stdlib.o \
                   $(OBJ_DIR)/user/lib/string.o \
                   $(OBJ_DIR)/user/syscall.o

all: $(USER_ELFS)

$(BUILD_DIR)/user/%.elf: $(USER_COMMON_OBJS) $(OBJ_DIR)/user/programs/%.o
	@mkdir -p $(dir $@)
	$(LD) $(USER_LDFLAGS) -o $@ $^

$(OBJ_DIR)/user/%.o: ../src/userland/%.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(OBJ_DIR)/user/crt0.o: ../src/userland/arch/x86_64/crt0.S
	@mkdir -p $(dir $@)
	nasm -f elf64 $< -o $@

clean:
	rm -rf $(BUILD_DIR)/user $(OBJ_DIR)/user

.SECONDARY: $(PROGRAM_OBJS)
.PHONY: all clean
