#include <drivers/keyboard.h>
#include <drivers/pic.h>
#include <drivers/serial.h>
#include <kernel/io.h>

#define KEYBOARD_DATA_PORT   0x60
#define KEY_BUFFER_SIZE      256

static char key_buffer[KEY_BUFFER_SIZE];
static size_t key_buffer_head = 0;
static size_t key_buffer_tail = 0;
static bool shift_pressed = false;
static bool caps_lock     = false;
static bool usb_hid_active = false;
static volatile uint32_t key_drop_count = 0;

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

static const char scancode_to_ascii[] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/', 0,
    '*', 0, ' '
};

static const char scancode_to_ascii_shift[] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,  'A','S','D','F','G','H','J','K','L',':','"','~',
    0,  '|','Z','X','C','V','B','N','M','<','>','?', 0,
    '*', 0, ' '
};

void keyboard_init(void) {
    key_buffer_head = 0;
    key_buffer_tail = 0;
    shift_pressed = false;
    caps_lock = false;
    usb_hid_active = false;
    key_drop_count = 0;
    pic_clear_mask(1);
    serial_write("[KB] init\n");
}

static void key_buffer_push(char c) {
    size_t next = (key_buffer_head + 1) % KEY_BUFFER_SIZE;
    if (next != key_buffer_tail) {
        key_buffer[key_buffer_head] = c;
        key_buffer_head = next;
    } else {
        key_drop_count++;
    }
}

void keyboard_handler(void) {
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
    if (usb_hid_active) {
        pic_send_eoi(1);
        return;
    }

    if (scancode == 0x2A || scancode == 0x36) { shift_pressed = true;  pic_send_eoi(1); return; }
    if (scancode == 0xAA || scancode == 0xB6) { shift_pressed = false; pic_send_eoi(1); return; }
    if (scancode == 0x3A) { caps_lock = !caps_lock;                     pic_send_eoi(1); return; }
    if (scancode & 0x80) {                                               pic_send_eoi(1); return; }

    char c = 0;
    if (scancode < sizeof(scancode_to_ascii) / sizeof(scancode_to_ascii[0])) {
        c = shift_pressed ? scancode_to_ascii_shift[scancode] : scancode_to_ascii[scancode];
        if (caps_lock) {
            if (c >= 'a' && c <= 'z') c -= 32;
            else if (c >= 'A' && c <= 'Z' && shift_pressed) c += 32;
        }
    }

    if (c != 0) {
        key_buffer_push(c);
    }

    pic_send_eoi(1);
}

void keyboard_inject_char(char c) {
    if (c == 0) return;
    uint64_t flags = irq_save_disable();
    key_buffer_push(c);
    irq_restore(flags);
}

char keyboard_getchar(void) {
    uint64_t flags = irq_save_disable();
    if (key_buffer_head == key_buffer_tail) {
        irq_restore(flags);
        return 0;
    }
    char c = key_buffer[key_buffer_tail];
    key_buffer_tail = (key_buffer_tail + 1) % KEY_BUFFER_SIZE;
    irq_restore(flags);
    return c;
}

bool keyboard_has_input(void) {
    uint64_t flags = irq_save_disable();
    bool has_input = (key_buffer_head != key_buffer_tail);
    irq_restore(flags);
    return has_input;
}

void keyboard_set_usb_hid_active(bool active) {
    uint64_t flags = irq_save_disable();
    bool changed = (usb_hid_active != active);
    usb_hid_active = active;
    irq_restore(flags);

    if (active) {
        pic_set_mask(1);
    } else {
        pic_clear_mask(1);
    }

    if (changed) {
        serial_write(active ? "[KB] PS/2 keyboard disabled (USB HID active)\n"
                            : "[KB] PS/2 keyboard enabled\n");
    }
}

uint32_t keyboard_get_drop_count(void) {
    uint64_t flags = irq_save_disable();
    uint32_t count = key_drop_count;
    irq_restore(flags);
    return count;
}
