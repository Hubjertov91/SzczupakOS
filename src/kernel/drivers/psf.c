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

static bool psf_validate_header(const psf1_header_t* header) {
    if (!header) return false;
    if (header->magic[0] != PSF1_MAGIC0 || header->magic[1] != PSF1_MAGIC1) return false;
    if (header->charsize == 0) return false;
    return true;
}

static bool psf_install_font_blob(const uint8_t* blob, size_t blob_size) {
    if (!blob || blob_size < sizeof(psf1_header_t)) {
        serial_write("[PSF] Blob too small\n");
        return false;
    }

    const psf1_header_t* header = (const psf1_header_t*)blob;
    if (!psf_validate_header(header)) {
        serial_write("[PSF] Invalid header\n");
        return false;
    }

    uint32_t next_chars_count = (header->mode & 0x01) ? 512 : 256;
    uint32_t next_char_height = header->charsize;
    uint32_t next_char_width = 8;
    size_t glyph_bytes = (size_t)next_chars_count * next_char_height;
    size_t needed = sizeof(psf1_header_t) + glyph_bytes;
    if (needed > blob_size) {
        serial_write("[PSF] Blob truncated\n");
        return false;
    }

    uint8_t* new_font_data = kmalloc(glyph_bytes);
    if (!new_font_data) {
        serial_write("[PSF] Failed to allocate memory\n");
        return false;
    }

    memcpy(new_font_data, blob + sizeof(psf1_header_t), glyph_bytes);
    if (font_data) {
        kfree(font_data);
    }
    font_data = new_font_data;
    char_width = next_char_width;
    char_height = next_char_height;
    chars_count = next_chars_count;

    serial_write("[PSF] Loaded font: ");
    serial_write_dec(char_width);
    serial_write("x");
    serial_write_dec(char_height);
    serial_write(" (");
    serial_write_dec(chars_count);
    serial_write(" chars)\n");
    return true;
}

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

    if (!psf_validate_header(&header)) {
        serial_write("[PSF] Invalid magic\n");
        vfs_close(file);
        return false;
    }

    char_height = header.charsize;
    char_width = 8;
    chars_count = (header.mode & 0x01) ? 512 : 256;

    size_t font_size = chars_count * char_height;
    uint8_t* loaded_font = kmalloc(font_size);
    if (!loaded_font) {
        serial_write("[PSF] Failed to allocate memory\n");
        vfs_close(file);
        return false;
    }

    if (!vfs_read(file, loaded_font, sizeof(header), font_size)) {
        serial_write("[PSF] Failed to read font data\n");
        kfree(loaded_font);
        vfs_close(file);
        return false;
    }

    if (font_data) {
        kfree(font_data);
    }
    font_data = loaded_font;

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

bool psf_load_from_memory(const void* data, size_t size) {
    if (!data || size == 0) {
        serial_write("[PSF] Invalid memory blob\n");
        return false;
    }
    return psf_install_font_blob((const uint8_t*)data, size);
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
