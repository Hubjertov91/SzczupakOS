#include <gui.h>
#include <stddef.h>
#include <syscall.h>

static uint32_t g_font_width = GUI_FONT_WIDTH;
static uint32_t g_font_height = GUI_FONT_HEIGHT;

uint32_t gui_font_width(void) {
    return g_font_width ? g_font_width : GUI_FONT_WIDTH;
}

uint32_t gui_font_height(void) {
    return g_font_height ? g_font_height : GUI_FONT_HEIGHT;
}

long gui_get_fb_info(gui_fb_info_t* out) {
    if (!out) return -1;

    struct fb_info info;
    long rc = sys_fb_info(&info);
    if (rc < 0) return rc;

    out->width = info.width;
    out->height = info.height;
    out->bpp = info.bpp;
    out->font_width = info.font_width ? info.font_width : GUI_FONT_WIDTH;
    out->font_height = info.font_height ? info.font_height : GUI_FONT_HEIGHT;

    g_font_width = out->font_width;
    g_font_height = out->font_height;
    return 0;
}

long gui_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    return sys_fb_putpixel(x, y, color);
}

long gui_getpixel(uint32_t x, uint32_t y, uint32_t* out_color) {
    if (!out_color) return -1;
    long rc = sys_fb_getpixel(x, y);
    if (rc < 0) return rc;
    *out_color = (uint32_t)rc;
    return 0;
}

long gui_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (w == 0 || h == 0) return 0;
    return sys_fb_rect(x, y, w, h, color);
}

long gui_clear(uint32_t color) {
    return sys_fb_clear(color);
}

long gui_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg) {
    return sys_fb_putchar_psf(x, y, c, fg, bg);
}

long gui_draw_text(uint32_t x, uint32_t y, const char* text, uint32_t fg, uint32_t bg) {
    if (!text) return -1;

    uint32_t font_w = gui_font_width();
    uint32_t font_h = gui_font_height();
    uint32_t start_x = x;
    uint32_t cx = x;
    uint32_t cy = y;

    for (const char* p = text; *p; p++) {
        if (*p == '\n') {
            cx = start_x;
            cy += font_h;
            continue;
        }
        long rc = gui_draw_char(cx, cy, *p, fg, bg);
        if (rc < 0) return rc;
        cx += font_w;
    }
    return 0;
}

void gui_draw_frame(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (w == 0 || h == 0) return;

    gui_fill_rect(x, y, w, 1, color);
    if (h > 1) {
        gui_fill_rect(x, y + h - 1, w, 1, color);
    }

    if (h > 2) {
        gui_fill_rect(x, y + 1, 1, h - 2, color);
        if (w > 1) {
            gui_fill_rect(x + w - 1, y + 1, 1, h - 2, color);
        }
    }
}

void gui_draw_window(const gui_window_t* win) {
    if (!win) return;
    if (win->x < 0 || win->y < 0) return;
    if (win->width < 4 || win->height < 4) return;

    uint32_t x = (uint32_t)win->x;
    uint32_t y = (uint32_t)win->y;
    uint32_t w = win->width;
    uint32_t h = win->height;

    gui_fill_rect(x, y, w, h, win->border_color);

    uint32_t title_h = gui_font_height() + 6;
    if (title_h + 2 > h) {
        title_h = h - 2;
    }

    gui_fill_rect(x + 1, y + 1, w - 2, title_h, win->title_bg_color);

    uint32_t body_y = y + 1 + title_h;
    uint32_t body_h = (h > title_h + 2) ? (h - title_h - 2) : 0;
    if (body_h > 0) {
        gui_fill_rect(x + 1, body_y, w - 2, body_h, win->body_color);
    }

    if (win->title) {
        gui_draw_text(x + 8, y + 4, win->title, win->title_fg_color, win->title_bg_color);
    }

    if (w >= 38 && title_h >= 12) {
        uint32_t button_size = 10;
        uint32_t by = y + 4;
        uint32_t bx = x + w - button_size - 6;
        gui_fill_rect(bx, by, button_size, button_size, GUI_COLOR_RGB(195, 62, 79));
        gui_draw_frame(bx, by, button_size, button_size, GUI_COLOR_RGB(246, 191, 198));
    }
}
