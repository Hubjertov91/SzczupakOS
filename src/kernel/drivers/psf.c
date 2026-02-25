#include <drivers/psf.h>
#include <drivers/framebuffer.h>
#include <drivers/serial.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <kernel/string.h>

static uint8_t* font_data = NULL;
static uint32_t char_width = 8;
static uint32_t char_height = 16;
static uint32_t chars_count = 256;

static void psf_draw_fallback_char(uint32_t x, uint32_t y, char c, fb_color_t fg, fb_color_t bg) {
    fb_fill_rect(x, y, 8, 16, bg);
    if (c == ' ') return;

    fb_fill_rect(x + 1, y + 1, 6, 14, fg);
    fb_fill_rect(x + 2, y + 2, 4, 12, bg);
}

bool psf_load(const char* path) {
    vfs_node_t* file = vfs_open(path, 0);
    if (!file) {
        serial_write("[PSF] Failed to open font file\n");
        return false;
    }

    psf1_header_t header;
    if (!vfs_read(file, &header, 0, sizeof(header))) {
        serial_write("[PSF] Failed to read header\n");
        vfs_close(file);
        return false;
    }

    if (header.magic[0] != PSF1_MAGIC0 || header.magic[1] != PSF1_MAGIC1) {
        serial_write("[PSF] Invalid magic\n");
        vfs_close(file);
        return false;
    }

    char_height = header.charsize;
    char_width = 8;
    chars_count = (header.mode & 0x01) ? 512 : 256;

    size_t font_size = chars_count * char_height;
    font_data = kmalloc(font_size);
    if (!font_data) {
        serial_write("[PSF] Failed to allocate memory\n");
        vfs_close(file);
        return false;
    }

    if (!vfs_read(file, font_data, sizeof(header), font_size)) {
        serial_write("[PSF] Failed to read font data\n");
        kfree(font_data);
        font_data = NULL;
        vfs_close(file);
        return false;
    }

    vfs_close(file);
    
    serial_write("[PSF] Loaded font: ");
    serial_write_dec(char_width);
    serial_write("x");
    serial_write_dec(char_height);
    serial_write(" (");
    serial_write_dec(chars_count);
    serial_write(" chars)\n");
    
    return true;
}

void psf_draw_char(uint32_t x, uint32_t y, char c, fb_color_t fg, fb_color_t bg) {
    if (!font_data) {
        psf_draw_fallback_char(x, y, c, fg, bg);
        return;
    }

    uint8_t ch = (uint8_t)c;
    if (ch >= chars_count) ch = 0;

    uint8_t* glyph = font_data + (ch * char_height);
    fb_draw_mono8(x, y, glyph, char_height, fg, bg);
}

uint32_t psf_get_width(void) {
    return char_width;
}

uint32_t psf_get_height(void) {
    return char_height;
}
