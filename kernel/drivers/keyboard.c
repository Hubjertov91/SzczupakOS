#include <drivers/keyboard.h>
#include <drivers/pic.h>
#include <kernel/serial.h>
#include <kernel/vga.h>
#include <kernel/shell.h>
#include <kernel/io.h>
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

#define KEY_BUFFER_SIZE 256

static char key_buffer[KEY_BUFFER_SIZE];
static size_t key_buffer_head = 0;
static size_t key_buffer_tail = 0;

static bool shift_pressed = false;
static bool caps_lock = false;

 
static const char scancode_to_ascii[] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

static const char scancode_to_ascii_shift[] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' '
};

void keyboard_init(void) {
     key_buffer_head = 0;
    key_buffer_tail = 0;
     
    pic_clear_mask(1);
    
    serial_write("[Keyboard] PS/2 Keyboard initialized\n");
}

static void key_buffer_push(char c) {
    size_t next = (key_buffer_head + 1) % KEY_BUFFER_SIZE;
    if (next != key_buffer_tail) {
        key_buffer[key_buffer_head] = c;
        key_buffer_head = next;
    }
}

void keyboard_handler(void) {
    serial_write("[KB] Interrupt received\n");
    uint8_t scancode = inb(KEYBOARD_DATA_PORT);
     
    if (scancode == 0x2A || scancode == 0x36) {  
        shift_pressed = true;
        pic_send_eoi(1);
        return;
    }
    
    if (scancode == 0xAA || scancode == 0xB6) { 
        shift_pressed = false;
        pic_send_eoi(1);
        return;
    }
    
    if (scancode == 0x3A) { 
        caps_lock = !caps_lock;
        pic_send_eoi(1);
        return;
    }
     
    if (scancode & 0x80) {
        pic_send_eoi(1);
        return;
    }
     
    char c = 0;
    
    if (scancode < sizeof(scancode_to_ascii)) {
        if (shift_pressed) {
            c = scancode_to_ascii_shift[scancode];
        } else {
            c = scancode_to_ascii[scancode];
        }
        
        if (caps_lock && c >= 'a' && c <= 'z') {
            c -= 32; 
        } else if (caps_lock && c >= 'A' && c <= 'Z' && shift_pressed) {
            c += 32;
        }
    }
    
    if (c != 0) {
        key_buffer_push(c);
    }
    
    pic_send_eoi(1);
}

char keyboard_getchar(void) {
    if (key_buffer_head == key_buffer_tail) {
        return 0; 
    }
    
    char c = key_buffer[key_buffer_tail];
    key_buffer_tail = (key_buffer_tail + 1) % KEY_BUFFER_SIZE;
    return c;
}

bool keyboard_has_input(void) {
    return key_buffer_head != key_buffer_tail;
}