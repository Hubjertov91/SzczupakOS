#ifndef _KERNEL_FRAMEBUFFER_H
#define _KERNEL_FRAMEBUFFER_H

#include "stdint.h"
#include <kernel/multiboot2.h>

typedef struct {
    uint64_t address;
    void*    virt_address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t  bpp;
    uint8_t  type;
    uint8_t  red_position;
    uint8_t  red_size;
    uint8_t  green_position;
    uint8_t  green_size;
    uint8_t  blue_position;
    uint8_t  blue_size;
    uint32_t bytes_per_pixel;
    size_t   buffer_size;
} framebuffer_info_t;

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} __attribute__((packed)) fb_color_t;

bool framebuffer_init(struct multiboot_tag_framebuffer* fb_tag);
framebuffer_info_t* framebuffer_get_info(void);
bool framebuffer_available(void);
void fb_putpixel(uint32_t x, uint32_t y, fb_color_t color);
bool fb_getpixel_rgb(uint32_t x, uint32_t y, uint32_t* out_rgb);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, fb_color_t color);
void fb_clear(fb_color_t color);
void fb_putchar(uint32_t x, uint32_t y, char c, fb_color_t fg, fb_color_t bg);
void fb_write_string(uint32_t x, uint32_t y, const char* str, fb_color_t fg, fb_color_t bg);
void fb_draw_mono8(uint32_t x, uint32_t y, const uint8_t* rows, uint32_t row_count, fb_color_t fg, fb_color_t bg);

#endif
