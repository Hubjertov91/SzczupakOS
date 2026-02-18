CC = gcc
LD = ld

USER_CFLAGS = -m64 -ffreestanding -nostdlib -nostdinc -fno-pie -O0 -I../include/user
USER_LDFLAGS = -nostdlib -T ../src/userland/ld/user.ld

BUILD_DIR = ../build
OBJ_DIR = $(BUILD_DIR)/obj

USER_OBJS = $(OBJ_DIR)/user/crt0.o \
            $(OBJ_DIR)/user/lib/math.o \
            $(OBJ_DIR)/user/lib/stdio.o \
            $(OBJ_DIR)/user/lib/stdlib.o \
            $(OBJ_DIR)/user/lib/string.o \
            $(OBJ_DIR)/user/syscall.o \
            $(OBJ_DIR)/user/programs/shell.o

all: $(BUILD_DIR)/user/shell.elf $(BUILD_DIR)/user/test.elf

$(BUILD_DIR)/user/shell.elf: $(USER_OBJS)
	@mkdir -p $(dir $@)
	$(LD) $(USER_LDFLAGS) -o $@ $^

$(BUILD_DIR)/user/test.elf: $(OBJ_DIR)/user/crt0.o $(OBJ_DIR)/user/lib/math.o $(OBJ_DIR)/user/lib/stdio.o $(OBJ_DIR)/user/lib/stdlib.o $(OBJ_DIR)/user/lib/string.o $(OBJ_DIR)/user/syscall.o $(OBJ_DIR)/user/programs/test.o
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

.PHONY: all clean
