#include <drivers/framebuffer.h>
#include <drivers/psf.h>
#include <mm/vmm.h>
#include <drivers/serial.h>
#include <kernel/string.h>

static framebuffer_info_t fb_info = {0};
static bool fb_initialized = false;

static inline uint8_t* fb_pixel_ptr(uint32_t x, uint32_t y) {
    return (uint8_t*)fb_info.virt_address + ((uint64_t)y * fb_info.pitch) + ((uint64_t)x * fb_info.bytes_per_pixel);
}

static inline uint32_t fb_pack_color32(fb_color_t color) {
    return ((uint32_t)color.a << 24) | ((uint32_t)color.r << 16) | ((uint32_t)color.g << 8) | (uint32_t)color.b;
}

static inline uint16_t fb_pack_color16(fb_color_t color) {
    uint16_t r = (uint16_t)(color.r >> 3);
    uint16_t g = (uint16_t)(color.g >> 2);
    uint16_t b = (uint16_t)(color.b >> 3);
    return (uint16_t)((r << 11) | (g << 5) | b);
}

static inline uint8_t fb_pack_color8(fb_color_t color) {
    /* RGB332 fallback for palettized 8-bit framebuffers. */
    uint8_t r = (uint8_t)(color.r & 0xE0u);
    uint8_t g = (uint8_t)((color.g & 0xE0u) >> 3);
    uint8_t b = (uint8_t)(color.b >> 6);
    return (uint8_t)(r | g | b);
}

static bool fb_clip_rect(uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h) {
    if (!x || !y || !w || !h) return false;
    if (*w == 0 || *h == 0) return false;
    if (*x >= fb_info.width || *y >= fb_info.height) return false;

    uint32_t max_w = fb_info.width - *x;
    uint32_t max_h = fb_info.height - *y;
    if (*w > max_w) *w = max_w;
    if (*h > max_h) *h = max_h;
    return (*w > 0 && *h > 0);
}

static void fb_fill_rect_32(uint32_t x, uint32_t y, uint32_t w, uint32_t h, fb_color_t color) {
    uint32_t pixel = fb_pack_color32(color);
    uint64_t pixel64 = ((uint64_t)pixel << 32) | pixel;

    for (uint32_t row = 0; row < h; row++) {
        uint32_t remaining = w;
        uint32_t* dst32 = (uint32_t*)fb_pixel_ptr(x, y + row);

        if (((uint64_t)dst32 & 0x7ULL) && remaining) {
            *dst32++ = pixel;
            remaining--;
        }

        uint64_t* dst64 = (uint64_t*)dst32;
        while (remaining >= 2) {
            *dst64++ = pixel64;
            remaining -= 2;
        }

        dst32 = (uint32_t*)dst64;
        if (remaining) {
            *dst32 = pixel;
        }
    }
}

static void fb_fill_rect_24(uint32_t x, uint32_t y, uint32_t w, uint32_t h, fb_color_t color) {
    for (uint32_t row = 0; row < h; row++) {
        uint8_t* dst = fb_pixel_ptr(x, y + row);
        for (uint32_t col = 0; col < w; col++) {
            dst[0] = color.b;
            dst[1] = color.g;
            dst[2] = color.r;
            dst += 3;
        }
    }
}

static void fb_fill_rect_16(uint32_t x, uint32_t y, uint32_t w, uint32_t h, fb_color_t color) {
    uint16_t pixel = fb_pack_color16(color);
    uint32_t pair = ((uint32_t)pixel << 16) | pixel;

    for (uint32_t row = 0; row < h; row++) {
        uint32_t remaining = w;
        uint16_t* dst16 = (uint16_t*)fb_pixel_ptr(x, y + row);

        if (((uint64_t)dst16 & 0x3ULL) && remaining) {
            *dst16++ = pixel;
            remaining--;
        }

        uint32_t* dst32 = (uint32_t*)dst16;
        while (remaining >= 2) {
            *dst32++ = pair;
            remaining -= 2;
        }

        dst16 = (uint16_t*)dst32;
        if (remaining) {
            *dst16 = pixel;
        }
    }
}

static void fb_fill_rect_8(uint32_t x, uint32_t y, uint32_t w, uint32_t h, fb_color_t color) {
    uint8_t pixel = fb_pack_color8(color);
    for (uint32_t row = 0; row < h; row++) {
        uint8_t* dst = fb_pixel_ptr(x, y + row);
        for (uint32_t col = 0; col < w; col++) {
            dst[col] = pixel;
        }
    }
}

static void fb_fill_rect_generic(uint32_t x, uint32_t y, uint32_t w, uint32_t h, fb_color_t color) {
    uint8_t packed[8] = {0};
    if (fb_info.bytes_per_pixel > sizeof(packed)) return;

    packed[0] = color.b;
    if (fb_info.bytes_per_pixel > 1) packed[1] = color.g;
    if (fb_info.bytes_per_pixel > 2) packed[2] = color.r;
    if (fb_info.bytes_per_pixel > 3) packed[3] = color.a;

    for (uint32_t row = 0; row < h; row++) {
        uint8_t* dst = fb_pixel_ptr(x, y + row);
        for (uint32_t col = 0; col < w; col++) {
            for (uint32_t i = 0; i < fb_info.bytes_per_pixel; i++) {
                dst[i] = packed[i];
            }
            dst += fb_info.bytes_per_pixel;
        }
    }
}

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

    if (fb_info.type == 0u) {
        serial_write("[FB] Indexed framebuffer detected, using RGB332 fallback\n");
    } else if (fb_info.type != 1u) {
        serial_write("[FB] Unsupported framebuffer type, falling back to VGA text\n");
        return false;
    }
    if (fb_info.bytes_per_pixel == 0 || fb_info.width == 0 || fb_info.height == 0) {
        serial_write("[FB] Invalid framebuffer geometry\n");
        return false;
    }

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

    uint8_t* pixel = fb_pixel_ptr(x, y);
    if (fb_info.bpp == 32) {
        *(uint32_t*)pixel = fb_pack_color32(color);
        return;
    }
    if (fb_info.bpp == 24) {
        pixel[0] = color.b;
        pixel[1] = color.g;
        pixel[2] = color.r;
        return;
    }
    if (fb_info.bpp == 16) {
        *(uint16_t*)pixel = fb_pack_color16(color);
        return;
    }
    if (fb_info.bpp == 8) {
        *pixel = fb_pack_color8(color);
        return;
    }

    uint8_t packed[8] = {0};
    if (fb_info.bytes_per_pixel > sizeof(packed)) return;

    packed[0] = color.b;
    if (fb_info.bytes_per_pixel > 1) packed[1] = color.g;
    if (fb_info.bytes_per_pixel > 2) packed[2] = color.r;
    if (fb_info.bytes_per_pixel > 3) packed[3] = color.a;
    for (uint32_t i = 0; i < fb_info.bytes_per_pixel; i++) {
        pixel[i] = packed[i];
    }
}

bool fb_getpixel_rgb(uint32_t x, uint32_t y, uint32_t* out_rgb) {
    if (!fb_initialized || !out_rgb) return false;
    if (x >= fb_info.width || y >= fb_info.height) return false;

    uint8_t* pixel = fb_pixel_ptr(x, y);
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    if (fb_info.bpp == 32) {
        uint32_t px = *(uint32_t*)pixel;
        r = (uint8_t)((px >> 16) & 0xFFu);
        g = (uint8_t)((px >> 8) & 0xFFu);
        b = (uint8_t)(px & 0xFFu);
    } else if (fb_info.bpp == 24) {
        b = pixel[0];
        g = pixel[1];
        r = pixel[2];
    } else if (fb_info.bpp == 16) {
        uint16_t px = *(uint16_t*)pixel;
        uint8_t r5 = (uint8_t)((px >> 11) & 0x1Fu);
        uint8_t g6 = (uint8_t)((px >> 5) & 0x3Fu);
        uint8_t b5 = (uint8_t)(px & 0x1Fu);
        r = (uint8_t)((r5 * 255u) / 31u);
        g = (uint8_t)((g6 * 255u) / 63u);
        b = (uint8_t)((b5 * 255u) / 31u);
    } else if (fb_info.bpp == 8) {
        uint8_t px = *pixel;
        uint8_t r3 = (uint8_t)((px >> 5) & 0x07u);
        uint8_t g3 = (uint8_t)((px >> 2) & 0x07u);
        uint8_t b2 = (uint8_t)(px & 0x03u);
        r = (uint8_t)((r3 * 255u) / 7u);
        g = (uint8_t)((g3 * 255u) / 7u);
        b = (uint8_t)((b2 * 255u) / 3u);
    } else {
        if (fb_info.bytes_per_pixel >= 3) {
            b = pixel[0];
            g = pixel[1];
            r = pixel[2];
        } else if (fb_info.bytes_per_pixel >= 1) {
            r = pixel[0];
            g = pixel[0];
            b = pixel[0];
        }
    }

    *out_rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    return true;
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, fb_color_t color) {
    if (!fb_initialized) return;
    if (!fb_clip_rect(&x, &y, &w, &h)) return;

    if (fb_info.bpp == 32) {
        fb_fill_rect_32(x, y, w, h, color);
        return;
    }
    if (fb_info.bpp == 24) {
        fb_fill_rect_24(x, y, w, h, color);
        return;
    }
    if (fb_info.bpp == 16) {
        fb_fill_rect_16(x, y, w, h, color);
        return;
    }
    if (fb_info.bpp == 8) {
        fb_fill_rect_8(x, y, w, h, color);
        return;
    }
    fb_fill_rect_generic(x, y, w, h, color);
}

void fb_clear(fb_color_t color) {
    if (!fb_initialized) return;
    fb_fill_rect(0, 0, fb_info.width, fb_info.height, color);
}

void fb_putchar(uint32_t x, uint32_t y, char c, fb_color_t fg, fb_color_t bg) {
    if (!fb_initialized) return;
    psf_draw_char(x, y, c, fg, bg);
}

void fb_draw_mono8(uint32_t x, uint32_t y, const uint8_t* rows, uint32_t row_count, fb_color_t fg, fb_color_t bg) {
    if (!fb_initialized || !rows || row_count == 0) return;
    if (x >= fb_info.width || y >= fb_info.height) return;

    uint32_t draw_w = fb_info.width - x;
    uint32_t draw_h = fb_info.height - y;
    if (draw_w > 8) draw_w = 8;
    if (draw_h > row_count) draw_h = row_count;
    if (draw_w == 0 || draw_h == 0) return;

    if (fb_info.bpp == 32) {
        uint32_t fg32 = fb_pack_color32(fg);
        uint32_t bg32 = fb_pack_color32(bg);

        for (uint32_t row = 0; row < draw_h; row++) {
            uint32_t* dst = (uint32_t*)fb_pixel_ptr(x, y + row);
            uint8_t bits = rows[row];
            for (uint32_t col = 0; col < draw_w; col++) {
                dst[col] = (bits & (uint8_t)(0x80u >> col)) ? fg32 : bg32;
            }
        }
        return;
    }

    if (fb_info.bpp == 24) {
        for (uint32_t row = 0; row < draw_h; row++) {
            uint8_t* dst = fb_pixel_ptr(x, y + row);
            uint8_t bits = rows[row];
            for (uint32_t col = 0; col < draw_w; col++) {
                bool fg_px = (bits & (uint8_t)(0x80u >> col)) != 0;
                dst[0] = fg_px ? fg.b : bg.b;
                dst[1] = fg_px ? fg.g : bg.g;
                dst[2] = fg_px ? fg.r : bg.r;
                dst += 3;
            }
        }
        return;
    }

    if (fb_info.bpp == 16) {
        uint16_t fg16 = fb_pack_color16(fg);
        uint16_t bg16 = fb_pack_color16(bg);

        for (uint32_t row = 0; row < draw_h; row++) {
            uint16_t* dst = (uint16_t*)fb_pixel_ptr(x, y + row);
            uint8_t bits = rows[row];
            for (uint32_t col = 0; col < draw_w; col++) {
                dst[col] = (bits & (uint8_t)(0x80u >> col)) ? fg16 : bg16;
            }
        }
        return;
    }

    for (uint32_t row = 0; row < draw_h; row++) {
        uint8_t bits = rows[row];
        for (uint32_t col = 0; col < draw_w; col++) {
            fb_putpixel(x + col, y + row, (bits & (uint8_t)(0x80u >> col)) ? fg : bg);
        }
    }
}

void fb_write_string(uint32_t x, uint32_t y, const char* str, fb_color_t fg, fb_color_t bg) {
    if (!str) return;

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
