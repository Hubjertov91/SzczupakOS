#include <drivers/serial.h>
#include <kernel/io.h>


#define COM1 0x3F8
#define SERIAL_LOG_CAPACITY 8192u

static char serial_log_buf[SERIAL_LOG_CAPACITY];
static uint32_t serial_log_head = 0;
static uint32_t serial_log_count = 0;

static inline uint64_t irq_save_disable(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; pop %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags) {
    if (flags & (1ULL << 9)) {
        __asm__ volatile("sti" : : : "memory");
    }
}

static int is_transmit_empty(void) {
    return inb(COM1 + 5) & 0x20;
}

static int is_receive_ready(void) {
    return inb(COM1 + 5) & 0x01;
}

static inline void serial_hw_write_char(char c) {
    while (!is_transmit_empty()) {
        __asm__ volatile("pause");
    }
    outb(COM1, (uint8_t)c);
}

static inline void serial_log_push(char c) {
    serial_log_buf[serial_log_head] = c;
    serial_log_head = (serial_log_head + 1u) % SERIAL_LOG_CAPACITY;
    if (serial_log_count < SERIAL_LOG_CAPACITY) {
        serial_log_count++;
    }
}

static void serial_emit_char(char c, bool mirror_log) {
    if (mirror_log) {
        uint64_t flags = irq_save_disable();
        serial_log_push(c);
        irq_restore(flags);
    }
    serial_hw_write_char(c);
}

void serial_init(void) {
    serial_log_clear();
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

void serial_write_char(char c) {
    serial_emit_char(c, true);
}

void serial_write(const char* str) {
    if (!str) return;
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

size_t serial_log_snapshot(char* out, size_t out_size) {
    if (!out || out_size == 0u) {
        return 0u;
    }

    uint64_t flags = irq_save_disable();
    uint32_t available = serial_log_count;
    size_t copy_len = out_size;
    if (copy_len > (size_t)available) {
        copy_len = (size_t)available;
    }
    if (copy_len > 0u && out_size > copy_len) {
        out[copy_len] = '\0';
    }
    uint32_t start = (serial_log_head + SERIAL_LOG_CAPACITY - available) % SERIAL_LOG_CAPACITY;
    if (copy_len < (size_t)available) {
        start = (serial_log_head + SERIAL_LOG_CAPACITY - (uint32_t)copy_len) % SERIAL_LOG_CAPACITY;
    }
    for (size_t i = 0; i < copy_len; i++) {
        out[i] = serial_log_buf[(start + (uint32_t)i) % SERIAL_LOG_CAPACITY];
    }
    irq_restore(flags);
    return copy_len;
}

size_t serial_log_dump_tail(size_t max_bytes) {
    if (max_bytes == 0u) {
        return 0u;
    }

    uint64_t flags = irq_save_disable();
    uint32_t available = serial_log_count;
    size_t dump_len = max_bytes;
    if (dump_len > (size_t)available) {
        dump_len = (size_t)available;
    }
    uint32_t start = (serial_log_head + SERIAL_LOG_CAPACITY - available) % SERIAL_LOG_CAPACITY;
    if (dump_len < (size_t)available) {
        start = (serial_log_head + SERIAL_LOG_CAPACITY - (uint32_t)dump_len) % SERIAL_LOG_CAPACITY;
    }
    for (size_t i = 0; i < dump_len; i++) {
        serial_hw_write_char(serial_log_buf[(start + (uint32_t)i) % SERIAL_LOG_CAPACITY]);
    }
    irq_restore(flags);
    return dump_len;
}

void serial_log_clear(void) {
    uint64_t flags = irq_save_disable();
    serial_log_head = 0;
    serial_log_count = 0;
    irq_restore(flags);
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
