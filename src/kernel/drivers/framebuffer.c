#include <drivers/framebuffer.h>
#include <drivers/psf.h>
#include <mm/vmm.h>
#include <drivers/serial.h>
#include <kernel/string.h>

static framebuffer_info_t fb_info = {0};
static bool fb_initialized = false;

bool framebuffer_init(struct multiboot_tag_framebuffer* fb_tag) {
    if (!fb_tag) return false;

    fb_info.address = fb_tag->framebuffer_addr;
    fb_info.width = fb_tag->framebuffer_width;
    fb_info.height = fb_tag->framebuffer_height;
    fb_info.pitch = fb_tag->framebuffer_pitch;
    fb_info.bpp = fb_tag->framebuffer_bpp;
    fb_info.type = fb_tag->framebuffer_type;
    fb_info.bytes_per_pixel = (fb_info.bpp + 7) / 8;
    fb_info.buffer_size = fb_info.pitch * fb_info.height;

    serial_write("[FB] Init: ");
    serial_write_dec(fb_info.width);
    serial_write("x");
    serial_write_dec(fb_info.height);
    serial_write(" @ ");
    serial_write_dec(fb_info.bpp);
    serial_write(" bpp\n");

    uint64_t phys_start = fb_info.address & ~0xFFFULL;
    uint64_t phys_end = ((fb_info.address + fb_info.buffer_size) + 0xFFF) & ~0xFFFULL;
    size_t pages = (phys_end - phys_start) / 4096;

    serial_write("[FB] Mapping ");
    serial_write_dec(pages);
    serial_write(" pages from phys ");
    serial_write_hex(phys_start);
    serial_write("\n");

    fb_info.virt_address = (void*)0xFFFF800210000000ULL;

    page_directory_t* kernel_dir = vmm_get_kernel_directory();
    for (size_t i = 0; i < pages; i++) {
        uint64_t virt = (uint64_t)fb_info.virt_address + (i * 4096);
        uint64_t phys = phys_start + (i * 4096);
        if (!vmm_map_page(kernel_dir, virt, phys, PAGE_PRESENT | PAGE_WRITE)) {
            serial_write("[FB] Failed to map page ");
            serial_write_dec(i);
            serial_write("\n");
            return false;
        }
    }

    fb_info.virt_address = (void*)((uint64_t)fb_info.virt_address + (fb_info.address - phys_start));

    fb_initialized = true;
    serial_write("[FB] Initialized successfully at virt ");
    serial_write_hex((uint64_t)fb_info.virt_address);
    serial_write("\n");
    return true;
}

framebuffer_info_t* framebuffer_get_info(void) {
    return fb_initialized ? &fb_info : NULL;
}

bool framebuffer_available(void) {
    return fb_initialized;
}

void fb_putpixel(uint32_t x, uint32_t y, fb_color_t color) {
    if (!fb_initialized || x >= fb_info.width || y >= fb_info.height) return;

    uint32_t offset = y * fb_info.pitch + x * fb_info.bytes_per_pixel;
    
    if (offset + fb_info.bytes_per_pixel > fb_info.buffer_size) return;
    
    uint32_t* pixel = (uint32_t*)((uint64_t)fb_info.virt_address + offset);
    
    if (fb_info.bpp == 32) {
        *pixel = (color.a << 24) | (color.r << 16) | (color.g << 8) | color.b;
    } else if (fb_info.bpp == 24) {
        uint8_t* p = (uint8_t*)pixel;
        p[0] = color.b;
        p[1] = color.g;
        p[2] = color.r;
    }
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, fb_color_t color) {
    if (!fb_initialized) return;
    
    if (x >= fb_info.width) return;
    if (y >= fb_info.height) return;
    
    if (x + w > fb_info.width) w = fb_info.width - x;
    if (y + h > fb_info.height) h = fb_info.height - y;
    
    for (uint32_t cy = 0; cy < h; cy++) {
        for (uint32_t cx = 0; cx < w; cx++) {
            fb_putpixel(x + cx, y + cy, color);
        }
    }
}

void fb_clear(fb_color_t color) {
    fb_fill_rect(0, 0, fb_info.width, fb_info.height, color);
}

void fb_putchar(uint32_t x, uint32_t y, char c, fb_color_t fg, fb_color_t bg) {
    if (!fb_initialized) return;
    psf_draw_char(x, y, c, fg, bg);
}

void fb_write_string(uint32_t x, uint32_t y, const char* str, fb_color_t fg, fb_color_t bg) {
    uint32_t cx = x;
    uint32_t cy = y;
    uint32_t char_w = psf_get_width();
    uint32_t char_h = psf_get_height();
    if (char_w == 0) char_w = 8;
    if (char_h == 0) char_h = 16;
    
    for (size_t i = 0; str[i]; i++) {
        if (str[i] == '\n') {
            cx = x;
            cy += char_h;
            continue;
        }
        fb_putchar(cx, cy, str[i], fg, bg);
        cx += char_w;
        if (cx + char_w > fb_info.width) {
            cx = x;
            cy += char_h;
        }
    }
}
