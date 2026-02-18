#include <kernel/terminal.h>
#include <kernel/vga.h>
#include <drivers/serial.h>
#include <drivers/keyboard.h>

#define LINE_BUF_SIZE 256

static char line_buf[LINE_BUF_SIZE];
static size_t line_len = 0;
static size_t line_pos = 0;

void terminal_init(void) {
    line_len = 0;
    line_pos = 0;
}

void terminal_clear(void) {
    vga_clear();
    line_len = 0;
    line_pos = 0;
}

void terminal_write(const char* str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        vga_putchar(str[i]);
        serial_write_char(str[i]);
    }
}

size_t terminal_read(char* buf, size_t size) {
    if (size == 0) return 0;

    serial_write("[TERM] waiting\n");

    while (!keyboard_has_input()) {
        __asm__ volatile("sti; hlt; cli");
    }

    __asm__ volatile("sti");

    char c = keyboard_getchar();
    serial_write("[TERM] got char, returning to syscall\n");
    buf[0] = c;
    return 1;
}