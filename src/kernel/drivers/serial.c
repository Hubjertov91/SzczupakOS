#include <drivers/serial.h>
#include <kernel/io.h>


#define COM1 0x3F8

static int is_transmit_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

static int is_receive_ready(void) {
    return inb(COM1 + 5) & 0x01;
}

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

void serial_write_char(char c) {
    while (!is_transmit_empty());
    outb(COM1, c);
}

void serial_write(const char* str) {
    for (size_t i = 0; str[i] != '\0'; i++) {
        serial_write_char(str[i]);
    }
}

void serial_write_hex(uint64_t val) {
    const char hex_chars[] = "0123456789ABCDEF";
    serial_write("0x");
    
    bool started = false;
    for (int i = 60; i >= 0; i -= 4) {
        uint8_t digit = (val >> i) & 0xF;
        if (digit != 0 || started || i == 0) {
            serial_write_char(hex_chars[digit]);
            started = true;
        }
    }
}

void serial_write_dec(uint32_t val) {
    if (val == 0) {
        serial_write_char('0');
        return;
    }
    
    char buf[12];
    int i = 0;
    
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    
    while (i > 0) {
        serial_write_char(buf[--i]);
    }
}

bool serial_has_data(void) {
    return is_receive_ready() ? true : false;
}

char serial_read_char(void) {
    while (!is_receive_ready()) {
        __asm__ volatile("pause");
    }
    return (char)inb(COM1);
}
