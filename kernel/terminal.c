#include <kernel/terminal.h>
#include <kernel/vga.h>
#include <kernel/serial.h>
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
    }
}

static void terminal_do_readline(void) {
    line_len = 0;
    line_pos = 0;

    while (1) {
        __asm__ volatile("sti; hlt; cli");

        if (!keyboard_has_input()) continue;

        char c = keyboard_getchar();
        if (c == 0) continue;

        if (c == '\b') {
            if (line_len == 0) continue;
            line_len--;
            size_t cur_col = vga_get_col();
            size_t cur_row = vga_get_row();
            if (cur_col == 0) {
                vga_set_cursor(cur_row - 1, VGA_WIDTH - 1);
            } else {
                vga_set_cursor(cur_row, cur_col - 1);
            }
            vga_putchar_at(vga_get_row(), vga_get_col(), ' ');
            continue;
        }

        if (c == '\n') {
            vga_putchar('\n');
            line_buf[line_len++] = '\n';
            break;
        }

        if (line_len < LINE_BUF_SIZE - 1) {
            line_buf[line_len++] = c;
            vga_putchar(c);
        }
    }
}

size_t terminal_read(char* buf, size_t size) {
    if (line_pos >= line_len) {
        terminal_do_readline();
    }

    size_t available = line_len - line_pos;
    size_t to_copy = size < available ? size : available;

    for (size_t i = 0; i < to_copy; i++) {
        buf[i] = line_buf[line_pos++];
    }

    return to_copy;
}