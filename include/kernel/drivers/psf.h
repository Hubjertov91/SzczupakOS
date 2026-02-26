#ifndef _KERNEL_PSF_H
#define _KERNEL_PSF_H

#include "stdint.h"
#include <drivers/framebuffer.h>

#define PSF1_MAGIC0 0x36
#define PSF1_MAGIC1 0x04

typedef struct {
    uint8_t magic[2];
    uint8_t mode;
    uint8_t charsize;
} psf1_header_t;

bool psf_load(const char* path);
bool psf_load_from_memory(const void* data, size_t size);
void psf_draw_char(uint32_t x, uint32_t y, char c, fb_color_t fg, fb_color_t bg);
uint32_t psf_get_width(void);
uint32_t psf_get_height(void);

#endif
