#include <kernel/terminal.h>
#include <kernel/vga.h>
#include <drivers/serial.h>
#include <drivers/keyboard.h>
#include <drivers/framebuffer.h>
#include <drivers/psf.h>
#include <task/task.h>
#include <net/net.h>

#define LINE_BUF_SIZE 256
static uint32_t term_x = 0;
static uint32_t term_y = 0;
static fb_color_t fg_color = {.r = 0xAA, .g = 0xAA, .b = 0xAA, .a = 255};
static fb_color_t bg_color = {.r = 0x00, .g = 0x00, .b = 0x00, .a = 255};

__attribute__((noinline)) void terminal_wait_input(void) {
    task_t* task = get_current_task();
    bool allow_preempt = (task && !task->is_kernel);
    if (allow_preempt) {
        task->kernel_preempt_ok = true;
    }

    while (!keyboard_has_input() && !serial_has_data()) {
        __asm__ volatile("sti; hlt; cli");
        net_poll();
    }

    if (allow_preempt) {
        task->kernel_preempt_ok = false;
    }
}

__attribute__((noinline)) void terminal_wait_input_end_marker(void) {
}

void terminal_init(void) {
    term_x = 0;
    term_y = 0;
}

void terminal_clear(void) {
    if (framebuffer_available()) {
        fb_clear(bg_color);
    } else {
        vga_clear();
    }
    term_x = 0;
    term_y = 0;
}

void terminal_write(const char* str, size_t len) {
    uint32_t char_w = psf_get_width();
    uint32_t char_h = psf_get_height();
    if (char_w == 0) char_w = 8;
    if (char_h == 0) char_h = 16;

    framebuffer_info_t* fb = framebuffer_get_info();
    uint32_t fb_w = (fb && fb->width) ? fb->width : 1024;
    uint32_t fb_h = (fb && fb->height) ? fb->height : 768;

    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        
        if (framebuffer_available()) {
            if (c == '\n') {
                term_x = 0;
                term_y += char_h;
                if (term_y + char_h > fb_h) {
                    term_y = 0;
                    terminal_clear();
                }
            } else if (c == '\t') {
                uint32_t tab = 4 * char_w;
                term_x = ((term_x + tab) / tab) * tab;
                if (term_x + char_w > fb_w) {
                    term_x = 0;
                    term_y += char_h;
                    if (term_y + char_h > fb_h) {
                        term_y = 0;
                        terminal_clear();
                    }
                }
            } else if (c == '\b') {
                if (term_x >= char_w) {
                    term_x -= char_w;
                }
            } else if (c >= 32 && c < 127) {
                psf_draw_char(term_x, term_y, c, fg_color, bg_color);
                term_x += char_w;
                if (term_x + char_w > fb_w) {
                    term_x = 0;
                    term_y += char_h;
                    if (term_y + char_h > fb_h) {
                        term_y = 0;
                        terminal_clear();
                    }
                }
            }
        } else {
            vga_putchar(c);
        }
        
        serial_write_char(c);
    }
}

size_t terminal_read(char* buf, size_t size) {
    if (size == 0) return 0;

    terminal_wait_input();

    char c = 0;
    if (keyboard_has_input()) {
        c = keyboard_getchar();
    } else if (serial_has_data()) {
        c = serial_read_char();
    } else {
        return 0;
    }
    if (c == '\r') c = '\n';
    buf[0] = c;
    return 1;
}
