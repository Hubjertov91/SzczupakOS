#include <kernel/vga.h>

static uint16_t* const vga_buffer = (uint16_t*)0xB8000;
static size_t vga_row = 0;
static size_t vga_col = 0;
static uint8_t vga_color = 0x0F;

void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_color = fg | (bg << 4);
}

void vga_init(void) {
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = ((uint16_t)vga_color << 8) | ' ';
    }
    vga_row = 0;
    vga_col = 0;
}

void vga_clear(void) {
    vga_init();
}

static void vga_scroll(void) {
    for (size_t i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++) {
        vga_buffer[i] = vga_buffer[i + VGA_WIDTH];
    }

    for (size_t i = (VGA_HEIGHT - 1) * VGA_WIDTH;
         i < VGA_HEIGHT * VGA_WIDTH; i++) {
        vga_buffer[i] = ((uint16_t)vga_color << 8) | ' ';
    }

    vga_row = VGA_HEIGHT - 1;
}

void vga_putchar(char c) {
    if (c == '\n') {
        vga_col = 0;
        if (++vga_row == VGA_HEIGHT) {
            vga_scroll();
        }
        return;
    }

    if (c == '\t') {
        vga_col = (vga_col + 4) & ~3;
        if (vga_col >= VGA_WIDTH) {
            vga_col = 0;
            if (++vga_row == VGA_HEIGHT) {
                vga_scroll();
            }
        }
        return;
    }
    if (c == '\b') {
        if (vga_col > 0) vga_col--;
        vga_buffer[vga_row * VGA_WIDTH + vga_col] = ((uint16_t)vga_color << 8) | ' ';
        return;
    }

    size_t index = vga_row * VGA_WIDTH + vga_col;
    vga_buffer[index] = ((uint16_t)vga_color << 8) | (uint8_t)c;

    if (++vga_col == VGA_WIDTH) {
        vga_col = 0;
        if (++vga_row == VGA_HEIGHT) {
            vga_scroll();
        }
    }
}

void vga_putchar_at(size_t row, size_t col, char c) {
    size_t index = row * VGA_WIDTH + col;
    vga_buffer[index] = ((uint16_t)vga_color << 8) | (uint8_t)c;
}

void vga_write(const char* str) {
    for (size_t i = 0; str[i]; i++) {
        vga_putchar(str[i]);
    }
}

void vga_write_dec(uint64_t value) {
    char buf[21];
    size_t i = 0;

    if (value == 0) {
        vga_putchar('0');
        return;
    }

    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }

    while (i--) {
        vga_putchar(buf[i]);
    }
}

size_t vga_get_col(void) {
    return vga_col;
}

size_t vga_get_row(void) {
    return vga_row;
}

void vga_set_cursor(size_t row, size_t col) {
    vga_row = row;
    vga_col = col;
}