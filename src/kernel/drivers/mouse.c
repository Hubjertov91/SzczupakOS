#include <drivers/mouse.h>
#include <drivers/pic.h>
#include <drivers/serial.h>
#include <drivers/framebuffer.h>
#include <kernel/io.h>

#define PS2_DATA_PORT    0x60
#define PS2_STATUS_PORT  0x64
#define PS2_CMD_PORT     0x64

#define MOUSE_QUEUE_SIZE 64

static volatile uint8_t packet[3];
static volatile uint8_t packet_index = 0;

static volatile int32_t mouse_x = 512;
static volatile int32_t mouse_y = 384;
static volatile uint8_t mouse_buttons = 0;
static volatile uint32_t mouse_seq = 0;
static volatile uint32_t mouse_overrun_count = 0;
static bool usb_hid_active = false;

static mouse_event_t queue[MOUSE_QUEUE_SIZE];
static volatile uint32_t queue_head = 0;
static volatile uint32_t queue_tail = 0;

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

static inline void mouse_queue_push(const mouse_event_t* ev) {
    uint32_t next = (queue_head + 1) % MOUSE_QUEUE_SIZE;
    if (next == queue_tail) {
        mouse_overrun_count++;
        queue_tail = (queue_tail + 1) % MOUSE_QUEUE_SIZE;
    }
    queue[queue_head] = *ev;
    queue_head = next;
}

static bool ps2_wait_input_ready(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if ((inb(PS2_STATUS_PORT) & 0x02) == 0) return true;
    }
    return false;
}

static bool ps2_wait_output_ready(void) {
    for (uint32_t i = 0; i < 100000; i++) {
        if (inb(PS2_STATUS_PORT) & 0x01) return true;
    }
    return false;
}

static bool ps2_mouse_write(uint8_t value) {
    if (!ps2_wait_input_ready()) return false;
    outb(PS2_CMD_PORT, 0xD4);
    if (!ps2_wait_input_ready()) return false;
    outb(PS2_DATA_PORT, value);
    return true;
}

static bool ps2_mouse_expect_ack(void) {
    for (uint32_t i = 0; i < 16; i++) {
        if (!ps2_wait_output_ready()) return false;
        uint8_t data = inb(PS2_DATA_PORT);
        if (data == 0xFA) return true;
    }
    return false;
}

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void mouse_apply_delta(int32_t dx, int32_t dy, uint8_t buttons) {
    buttons &= 0x07u;

    int32_t screen_w = 1024;
    int32_t screen_h = 768;
    framebuffer_info_t* fb = framebuffer_get_info();
    if (fb && fb->width > 0 && fb->height > 0) {
        screen_w = (int32_t)fb->width;
        screen_h = (int32_t)fb->height;
    }

    int32_t nx = clamp_i32(mouse_x + dx, 0, screen_w - 1);
    int32_t ny = clamp_i32(mouse_y - dy, 0, screen_h - 1);
    uint8_t changed = buttons ^ mouse_buttons;

    if (nx == mouse_x && ny == mouse_y && changed == 0) {
        return;
    }

    mouse_x = nx;
    mouse_y = ny;
    mouse_buttons = buttons;
    mouse_seq++;

    mouse_event_t ev;
    ev.x = mouse_x;
    ev.y = mouse_y;
    ev.dx = dx;
    ev.dy = dy;
    ev.buttons = buttons;
    ev.changed = changed;
    ev._reserved = 0;
    ev.seq = mouse_seq;
    mouse_queue_push(&ev);
}

static void mouse_handle_packet(uint8_t b0, uint8_t b1, uint8_t b2) {
    int32_t dx = (int32_t)(int8_t)b1;
    int32_t dy = (int32_t)(int8_t)b2;

    if (b0 & 0x40) dx = 0;
    if (b0 & 0x80) dy = 0;
    uint64_t flags = irq_save_disable();
    mouse_apply_delta(dx, dy, b0 & 0x07u);
    irq_restore(flags);
}

void mouse_init(void) {
    framebuffer_info_t* fb = framebuffer_get_info();
    if (fb && fb->width > 0 && fb->height > 0) {
        mouse_x = (int32_t)(fb->width / 2);
        mouse_y = (int32_t)(fb->height / 2);
    }
    mouse_buttons = 0;
    mouse_seq = 0;
    mouse_overrun_count = 0;
    packet_index = 0;
    queue_head = 0;
    queue_tail = 0;
    usb_hid_active = false;

    if (ps2_wait_input_ready()) outb(PS2_CMD_PORT, 0xA8);

    uint8_t cmd_byte = 0;
    if (ps2_wait_input_ready()) outb(PS2_CMD_PORT, 0x20);
    if (ps2_wait_output_ready()) cmd_byte = inb(PS2_DATA_PORT);
    cmd_byte |= 0x02;
    cmd_byte &= (uint8_t)~0x20;
    if (ps2_wait_input_ready()) outb(PS2_CMD_PORT, 0x60);
    if (ps2_wait_input_ready()) outb(PS2_DATA_PORT, cmd_byte);

    if (!ps2_mouse_write(0xF6) || !ps2_mouse_expect_ack()) {
        serial_write("[MOUSE] Warning: failed to set defaults\n");
    }
    if (!ps2_mouse_write(0xF4) || !ps2_mouse_expect_ack()) {
        serial_write("[MOUSE] Warning: failed to enable data reporting\n");
    }

    pic_clear_mask(2);
    pic_clear_mask(12);
    serial_write("[MOUSE] init\n");
}

void mouse_handler(void) {
    if (usb_hid_active) {
        while (1) {
            uint8_t status = inb(PS2_STATUS_PORT);
            if ((status & 0x01) == 0u) break;
            if ((status & 0x20) == 0u) break;
            (void)inb(PS2_DATA_PORT);
        }
        pic_send_eoi(12);
        return;
    }

    uint32_t iter = 0;
    while (iter < 16) {
        uint8_t status = inb(PS2_STATUS_PORT);
        if (!(status & 0x01)) break;
        if (!(status & 0x20)) break;

        uint8_t data = inb(PS2_DATA_PORT);
        iter++;

        if (packet_index == 0 && !(data & 0x08)) {
            continue;
        }

        packet[packet_index++] = data;
        if (packet_index < 3) continue;

        packet_index = 0;
        mouse_handle_packet(packet[0], packet[1], packet[2]);
    }

    pic_send_eoi(12);
}

void mouse_inject_usb(int8_t dx, int8_t dy, uint8_t buttons) {
    uint64_t flags = irq_save_disable();
    mouse_apply_delta((int32_t)dx, (int32_t)dy, buttons);
    irq_restore(flags);
}

bool mouse_poll_event(mouse_event_t* out) {
    if (!out) return false;

    uint64_t flags = irq_save_disable();
    if (queue_head == queue_tail) {
        irq_restore(flags);
        return false;
    }

    *out = queue[queue_tail];
    queue_tail = (queue_tail + 1) % MOUSE_QUEUE_SIZE;
    irq_restore(flags);
    return true;
}

void mouse_set_usb_hid_active(bool active) {
    uint64_t flags = irq_save_disable();
    bool changed = (usb_hid_active != active);
    usb_hid_active = active;
    irq_restore(flags);

    if (active) {
        pic_set_mask(12);
    } else {
        pic_clear_mask(12);
    }

    if (changed) {
        serial_write(active ? "[MOUSE] PS/2 mouse disabled (USB HID active)\n"
                            : "[MOUSE] PS/2 mouse enabled\n");
    }
}

uint32_t mouse_get_overrun_count(void) {
    uint64_t flags = irq_save_disable();
    uint32_t count = mouse_overrun_count;
    irq_restore(flags);
    return count;
}
