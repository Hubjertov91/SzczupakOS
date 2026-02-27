#ifndef GUI_H
#define GUI_H

#include <stdint.h>

#define GUI_COLOR_RGB(r, g, b) \
    ((((uint32_t)(r) & 0xFFu) << 16) | (((uint32_t)(g) & 0xFFu) << 8) | ((uint32_t)(b) & 0xFFu))

#define GUI_FONT_WIDTH 8u
#define GUI_FONT_HEIGHT 16u

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t font_width;
    uint32_t font_height;
} gui_fb_info_t;

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t border_color;
    uint32_t title_bg_color;
    uint32_t title_fg_color;
    uint32_t body_color;
    const char* title;
} gui_window_t;

long gui_get_fb_info(gui_fb_info_t* out);
long gui_putpixel(uint32_t x, uint32_t y, uint32_t color);
long gui_getpixel(uint32_t x, uint32_t y, uint32_t* out_color);
long gui_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
long gui_clear(uint32_t color);
long gui_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
long gui_draw_text(uint32_t x, uint32_t y, const char* text, uint32_t fg, uint32_t bg);
void gui_draw_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void gui_draw_window(const gui_window_t* win);
uint32_t gui_font_width(void);
uint32_t gui_font_height(void);

#endif
