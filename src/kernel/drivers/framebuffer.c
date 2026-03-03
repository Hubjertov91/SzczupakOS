#include <drivers/framebuffer.h>
#include <drivers/psf.h>
#include <mm/heap.h>
#include <mm/vmm.h>
#include <drivers/serial.h>
#include <kernel/string.h>

#define FB_VIRT_BASE 0xFFFF800210000000ULL
#define MULTIBOOT_FB_RGB_TAG_MIN_SIZE 38u

static framebuffer_info_t* fb_cfg = NULL;
#define FB (*fb_cfg)
static bool fb_initialized = false;
static bool fb_runtime_warned = false;

static inline bool fb_is_canonical(uint64_t addr) {
    return (addr <= 0x00007FFFFFFFFFFFULL) || (addr >= 0xFFFF800000000000ULL);
}

static bool fb_ensure_runtime_sane(void) {
    if (!fb_initialized || !fb_cfg) return false;
    if (FB.virt_address && FB.width && FB.height && FB.pitch &&
        FB.bytes_per_pixel > 0u && FB.bytes_per_pixel <= 4u) {
        return true;
    }

    if (!fb_runtime_warned) {
        serial_write("[FB] Runtime state corrupted, writes will be skipped\n");
        serial_write("[FB] State virt=");
        serial_write_hex((uint64_t)FB.virt_address);
        serial_write(" width=");
        serial_write_dec(FB.width);
        serial_write(" height=");
        serial_write_dec(FB.height);
        serial_write(" pitch=");
        serial_write_dec(FB.pitch);
        serial_write(" bpp=");
        serial_write_dec((uint32_t)FB.bpp);
        serial_write(" bytespp=");
        serial_write_dec(FB.bytes_per_pixel);
        serial_write(" size=");
        serial_write_hex((uint64_t)FB.buffer_size);
        serial_write("\n");
        fb_runtime_warned = true;
    }
    return false;
}

static inline uint8_t* fb_pixel_ptr(uint32_t x, uint32_t y) {
    if (!fb_ensure_runtime_sane()) return NULL;
    if (x >= FB.width || y >= FB.height) return NULL;

    uint64_t row_off = (uint64_t)y * (uint64_t)FB.pitch;
    uint64_t col_off = (uint64_t)x * (uint64_t)FB.bytes_per_pixel;
    if (row_off > FB.buffer_size) return NULL;
    if (col_off > FB.buffer_size) return NULL;

    uint64_t offset = row_off + col_off;
    if (offset < row_off) return NULL;
    if (offset > FB.buffer_size - FB.bytes_per_pixel) return NULL;

    uint64_t addr = (uint64_t)FB.virt_address + offset;
    if (!fb_is_canonical(addr)) return NULL;
    if (!fb_is_canonical(addr + FB.bytes_per_pixel - 1u)) return NULL;
    return (uint8_t*)addr;
}

static inline uint8_t fb_pack_color8(fb_color_t color) {
    /* RGB332 fallback for palettized 8-bit framebuffers. */
    uint8_t r = (uint8_t)(color.r & 0xE0u);
    uint8_t g = (uint8_t)((color.g & 0xE0u) >> 3);
    uint8_t b = (uint8_t)(color.b >> 6);
    return (uint8_t)(r | g | b);
}

static inline uint32_t fb_mask_u32(uint8_t bits) {
    if (bits == 0u) return 0u;
    if (bits >= 32u) return 0xFFFFFFFFu;
    return (uint32_t)((1u << bits) - 1u);
}

static inline uint32_t fb_scale_8_to_n(uint8_t value8, uint8_t bits) {
    uint32_t mask = fb_mask_u32(bits);
    if (mask == 0u) return 0u;
    return (uint32_t)(((uint64_t)value8 * mask + 127u) / 255u);
}

static inline uint8_t fb_scale_n_to_8(uint32_t value_n, uint8_t bits) {
    uint32_t mask = fb_mask_u32(bits);
    if (mask == 0u) return 0u;
    if (value_n > mask) value_n = mask;
    return (uint8_t)(((uint64_t)value_n * 255u + (mask / 2u)) / mask);
}

static inline uint32_t fb_pack_native_color(fb_color_t color) {
    uint32_t value = 0u;
    value |= (fb_scale_8_to_n(color.r, FB.red_size) << FB.red_position);
    value |= (fb_scale_8_to_n(color.g, FB.green_size) << FB.green_position);
    value |= (fb_scale_8_to_n(color.b, FB.blue_size) << FB.blue_position);
    return value;
}

static inline void fb_store_native_pixel(uint8_t* pixel, uint32_t value) {
    if (!pixel) return;
    if (!fb_ensure_runtime_sane()) return;

    uint64_t addr = (uint64_t)pixel;
    uint64_t base = (uint64_t)FB.virt_address;
    if (!fb_is_canonical(addr) || !fb_is_canonical(base)) return;
    if (addr < base) return;
    uint64_t offset = addr - base;
    if (offset > FB.buffer_size - FB.bytes_per_pixel) return;

    if (FB.bytes_per_pixel == 4u) {
        *(uint32_t*)pixel = value;
        return;
    }

    if (FB.bytes_per_pixel == 2u) {
        *(uint16_t*)pixel = (uint16_t)(value & 0xFFFFu);
        return;
    }

    for (uint32_t i = 0; i < FB.bytes_per_pixel; i++) {
        pixel[i] = (uint8_t)((value >> (i * 8u)) & 0xFFu);
    }
}

static inline uint32_t fb_load_native_pixel(const uint8_t* pixel) {
    if (!pixel) return 0u;
    if (!fb_ensure_runtime_sane()) return 0u;

    uint64_t addr = (uint64_t)pixel;
    uint64_t base = (uint64_t)FB.virt_address;
    if (!fb_is_canonical(addr) || !fb_is_canonical(base)) return 0u;
    if (addr < base) return 0u;
    uint64_t offset = addr - base;
    if (offset > FB.buffer_size - FB.bytes_per_pixel) return 0u;

    if (FB.bytes_per_pixel == 4u) {
        return *(const uint32_t*)pixel;
    }

    if (FB.bytes_per_pixel == 2u) {
        return (uint32_t)*(const uint16_t*)pixel;
    }

    uint32_t value = 0u;
    for (uint32_t i = 0; i < FB.bytes_per_pixel; i++) {
        value |= ((uint32_t)pixel[i] << (i * 8u));
    }
    return value;
}

static bool fb_clip_rect(uint32_t* x, uint32_t* y, uint32_t* w, uint32_t* h) {
    if (!fb_ensure_runtime_sane()) return false;
    if (!x || !y || !w || !h) return false;
    if (*w == 0u || *h == 0u) return false;
    if (*x >= FB.width || *y >= FB.height) return false;

    uint32_t max_w = FB.width - *x;
    uint32_t max_h = FB.height - *y;
    if (*w > max_w) *w = max_w;
    if (*h > max_h) *h = max_h;
    return (*w > 0u && *h > 0u);
}

static void fb_apply_default_color_layout(void) {
    if (FB.bpp == 16u) {
        FB.red_position = 11u;
        FB.red_size = 5u;
        FB.green_position = 5u;
        FB.green_size = 6u;
        FB.blue_position = 0u;
        FB.blue_size = 5u;
        return;
    }

    /* Most linear framebuffers exposed by GRUB are little-endian xRGB/BGRX-like. */
    FB.red_position = 16u;
    FB.red_size = 8u;
    FB.green_position = 8u;
    FB.green_size = 8u;
    FB.blue_position = 0u;
    FB.blue_size = 8u;
}

static void fb_configure_color_layout(const struct multiboot_tag_framebuffer* fb_tag) {
    fb_apply_default_color_layout();

    if (!fb_tag || FB.type != 1u) {
        return;
    }

    if (fb_tag->size < MULTIBOOT_FB_RGB_TAG_MIN_SIZE) {
        serial_write("[FB] Color mask info not present in tag, using defaults\n");
        return;
    }

    if (fb_tag->framebuffer_red_mask_size == 0u ||
        fb_tag->framebuffer_green_mask_size == 0u ||
        fb_tag->framebuffer_blue_mask_size == 0u) {
        serial_write("[FB] Invalid color masks in tag, using defaults\n");
        return;
    }

    FB.red_position = fb_tag->framebuffer_red_field_position;
    FB.red_size = fb_tag->framebuffer_red_mask_size;
    FB.green_position = fb_tag->framebuffer_green_field_position;
    FB.green_size = fb_tag->framebuffer_green_mask_size;
    FB.blue_position = fb_tag->framebuffer_blue_field_position;
    FB.blue_size = fb_tag->framebuffer_blue_mask_size;
}

bool framebuffer_init(struct multiboot_tag_framebuffer* fb_tag) {
    if (!fb_tag) return false;

    if (!fb_cfg) {
        fb_cfg = (framebuffer_info_t*)kmalloc(sizeof(*fb_cfg));
        if (!fb_cfg) {
            serial_write("[FB] Failed to allocate runtime state\n");
            return false;
        }
    }
    memset(fb_cfg, 0, sizeof(*fb_cfg));

    fb_runtime_warned = false;

    FB.address = fb_tag->framebuffer_addr;
    FB.width = fb_tag->framebuffer_width;
    FB.height = fb_tag->framebuffer_height;
    FB.pitch = fb_tag->framebuffer_pitch;
    FB.bpp = fb_tag->framebuffer_bpp;
    FB.type = fb_tag->framebuffer_type;
    FB.bytes_per_pixel = (FB.bpp + 7u) / 8u;
    FB.buffer_size = (size_t)FB.pitch * (size_t)FB.height;

    if (FB.address == 0u || FB.width == 0u || FB.height == 0u ||
        FB.bytes_per_pixel == 0u || FB.bytes_per_pixel > 4u) {
        serial_write("[FB] Invalid framebuffer geometry\n");
        return false;
    }
    if ((uint64_t)FB.pitch < ((uint64_t)FB.width * (uint64_t)FB.bytes_per_pixel)) {
        serial_write("[FB] Invalid framebuffer pitch\n");
        return false;
    }

    if (FB.type == MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED) {
        serial_write("[FB] Indexed framebuffer detected, using RGB332 fallback\n");
    } else if (FB.type != 1u) {
        serial_write("[FB] Unsupported framebuffer type, falling back to VGA text\n");
        return false;
    }

    fb_configure_color_layout(fb_tag);

    serial_write("[FB] Init: ");
    serial_write_dec(FB.width);
    serial_write("x");
    serial_write_dec(FB.height);
    serial_write(" @ ");
    serial_write_dec(FB.bpp);
    serial_write(" bpp\n");

    if (FB.type == 1u) {
        serial_write("[FB] RGB layout R(");
        serial_write_dec(FB.red_position);
        serial_write(":");
        serial_write_dec(FB.red_size);
        serial_write(") G(");
        serial_write_dec(FB.green_position);
        serial_write(":");
        serial_write_dec(FB.green_size);
        serial_write(") B(");
        serial_write_dec(FB.blue_position);
        serial_write(":");
        serial_write_dec(FB.blue_size);
        serial_write(")\n");
    }

    uint64_t phys_start = FB.address & ~0xFFFULL;
    uint64_t phys_end = ((FB.address + FB.buffer_size) + 0xFFFULL) & ~0xFFFULL;
    size_t pages = (size_t)((phys_end - phys_start) / 4096u);

    serial_write("[FB] Mapping ");
    serial_write_dec((uint32_t)pages);
    serial_write(" pages from phys ");
    serial_write_hex(phys_start);
    serial_write("\n");

    FB.virt_address = (void*)FB_VIRT_BASE;

    page_directory_t* kernel_dir = vmm_get_kernel_directory();
    if (!kernel_dir) {
        serial_write("[FB] Missing kernel page directory\n");
        return false;
    }

    for (size_t i = 0; i < pages; i++) {
        uint64_t virt = (uint64_t)FB.virt_address + (i * 4096u);
        uint64_t phys = phys_start + (i * 4096u);
        if (!vmm_map_page(kernel_dir, virt, phys, PAGE_PRESENT | PAGE_WRITE)) {
            serial_write("[FB] Failed to map page ");
            serial_write_dec((uint32_t)i);
            serial_write("\n");
            return false;
        }
    }

    FB.virt_address = (void*)((uint64_t)FB.virt_address + (FB.address - phys_start));

    fb_initialized = true;
    serial_write("[FB] Initialized successfully at virt ");
    serial_write_hex((uint64_t)FB.virt_address);
    serial_write("\n");
    return true;
}

framebuffer_info_t* framebuffer_get_info(void) {
    return fb_ensure_runtime_sane() ? fb_cfg : NULL;
}

bool framebuffer_available(void) {
    return fb_ensure_runtime_sane();
}

void fb_putpixel(uint32_t x, uint32_t y, fb_color_t color) {
    if (!fb_ensure_runtime_sane() || x >= FB.width || y >= FB.height) return;

    uint8_t* pixel = fb_pixel_ptr(x, y);
    if (!pixel) return;

    if (FB.type == MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED && FB.bytes_per_pixel == 1u) {
        *pixel = fb_pack_color8(color);
        return;
    }

    fb_store_native_pixel(pixel, fb_pack_native_color(color));
}

bool fb_getpixel_rgb(uint32_t x, uint32_t y, uint32_t* out_rgb) {
    if (!fb_ensure_runtime_sane() || !out_rgb) return false;
    if (x >= FB.width || y >= FB.height) return false;

    const uint8_t* pixel = fb_pixel_ptr(x, y);
    if (!pixel) return false;

    if (FB.type == MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED && FB.bytes_per_pixel == 1u) {
        uint8_t px = *pixel;
        uint8_t r3 = (uint8_t)((px >> 5) & 0x07u);
        uint8_t g3 = (uint8_t)((px >> 2) & 0x07u);
        uint8_t b2 = (uint8_t)(px & 0x03u);
        uint8_t r = (uint8_t)((r3 * 255u) / 7u);
        uint8_t g = (uint8_t)((g3 * 255u) / 7u);
        uint8_t b = (uint8_t)((b2 * 255u) / 3u);
        *out_rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        return true;
    }

    uint32_t px = fb_load_native_pixel(pixel);
    uint32_t r_raw = (px >> FB.red_position) & fb_mask_u32(FB.red_size);
    uint32_t g_raw = (px >> FB.green_position) & fb_mask_u32(FB.green_size);
    uint32_t b_raw = (px >> FB.blue_position) & fb_mask_u32(FB.blue_size);

    uint8_t r = fb_scale_n_to_8(r_raw, FB.red_size);
    uint8_t g = fb_scale_n_to_8(g_raw, FB.green_size);
    uint8_t b = fb_scale_n_to_8(b_raw, FB.blue_size);

    *out_rgb = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
    return true;
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, fb_color_t color) {
    if (!fb_ensure_runtime_sane()) return;
    if (!fb_clip_rect(&x, &y, &w, &h)) return;

    if (FB.type == MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED && FB.bytes_per_pixel == 1u) {
        uint8_t pixel = fb_pack_color8(color);
        for (uint32_t row = 0; row < h; row++) {
            uint8_t* dst = fb_pixel_ptr(x, y + row);
            if (!dst) continue;
            for (uint32_t col = 0; col < w; col++) {
                dst[col] = pixel;
            }
        }
        return;
    }

    uint32_t packed = fb_pack_native_color(color);

    if (FB.bytes_per_pixel == 4u) {
        for (uint32_t row = 0; row < h; row++) {
            uint32_t* dst = (uint32_t*)fb_pixel_ptr(x, y + row);
            if (!dst) continue;
            for (uint32_t col = 0; col < w; col++) {
                dst[col] = packed;
            }
        }
        return;
    }

    if (FB.bytes_per_pixel == 2u) {
        uint16_t packed16 = (uint16_t)(packed & 0xFFFFu);
        for (uint32_t row = 0; row < h; row++) {
            uint16_t* dst = (uint16_t*)fb_pixel_ptr(x, y + row);
            if (!dst) continue;
            for (uint32_t col = 0; col < w; col++) {
                dst[col] = packed16;
            }
        }
        return;
    }

    for (uint32_t row = 0; row < h; row++) {
        uint8_t* dst = fb_pixel_ptr(x, y + row);
        if (!dst) continue;
        for (uint32_t col = 0; col < w; col++) {
            fb_store_native_pixel(dst, packed);
            dst += FB.bytes_per_pixel;
        }
    }
}

void fb_clear(fb_color_t color) {
    if (!fb_ensure_runtime_sane()) return;
    fb_fill_rect(0, 0, FB.width, FB.height, color);
}

void fb_putchar(uint32_t x, uint32_t y, char c, fb_color_t fg, fb_color_t bg) {
    if (!fb_ensure_runtime_sane()) return;
    psf_draw_char(x, y, c, fg, bg);
}

void fb_draw_mono8(uint32_t x, uint32_t y, const uint8_t* rows, uint32_t row_count, fb_color_t fg, fb_color_t bg) {
    if (!fb_ensure_runtime_sane() || !rows || row_count == 0u) return;
    if (x >= FB.width || y >= FB.height) return;

    uint32_t draw_w = FB.width - x;
    uint32_t draw_h = FB.height - y;
    if (draw_w > 8u) draw_w = 8u;
    if (draw_h > row_count) draw_h = row_count;
    if (draw_w == 0u || draw_h == 0u) return;

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
    if (char_w == 0u) char_w = 8u;
    if (char_h == 0u) char_h = 16u;

    for (size_t i = 0; str[i] != '\0'; i++) {
        if (str[i] == '\n') {
            cx = x;
            cy += char_h;
            continue;
        }

        fb_putchar(cx, cy, str[i], fg, bg);
        cx += char_w;
        if (cx + char_w > FB.width) {
            cx = x;
            cy += char_h;
        }
    }
}
