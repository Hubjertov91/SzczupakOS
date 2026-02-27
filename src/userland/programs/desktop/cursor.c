#include "desktop.h"

typedef struct {
    int8_t dx;
    int8_t dy;
    uint8_t kind;
} cursor_point_t;

enum {
    CURSOR_KIND_WHITE = 1,
    CURSOR_KIND_BLACK = 2
};

static const cursor_point_t k_cursor_points[CURSOR_POINT_COUNT] = {
    {0, 0, CURSOR_KIND_WHITE}, {1, 0, CURSOR_KIND_BLACK}, {2, 0, CURSOR_KIND_WHITE}, {3, 0, CURSOR_KIND_WHITE}, {4, 0, CURSOR_KIND_WHITE}, {5, 0, CURSOR_KIND_WHITE},
    {0, 1, CURSOR_KIND_BLACK}, {1, 1, CURSOR_KIND_BLACK}, {2, 1, CURSOR_KIND_BLACK}, {3, 1, CURSOR_KIND_BLACK}, {4, 1, CURSOR_KIND_BLACK}, {5, 1, CURSOR_KIND_BLACK},
    {0, 2, CURSOR_KIND_WHITE}, {1, 2, CURSOR_KIND_BLACK}, {2, 2, CURSOR_KIND_WHITE},
    {0, 3, CURSOR_KIND_WHITE}, {1, 3, CURSOR_KIND_BLACK}, {3, 3, CURSOR_KIND_WHITE},
    {0, 4, CURSOR_KIND_WHITE}, {1, 4, CURSOR_KIND_BLACK}, {4, 4, CURSOR_KIND_WHITE},
    {0, 5, CURSOR_KIND_WHITE}, {1, 5, CURSOR_KIND_BLACK}, {5, 5, CURSOR_KIND_WHITE},
    {0, 6, CURSOR_KIND_WHITE}, {1, 6, CURSOR_KIND_BLACK},
    {0, 7, CURSOR_KIND_WHITE}, {1, 7, CURSOR_KIND_BLACK},
    {0, 8, CURSOR_KIND_WHITE}, {1, 8, CURSOR_KIND_BLACK}, {3, 8, CURSOR_KIND_WHITE},
    {0, 9, CURSOR_KIND_WHITE}, {1, 9, CURSOR_KIND_BLACK}, {3, 9, CURSOR_KIND_WHITE},
    {3, 10, CURSOR_KIND_WHITE},
    {3, 11, CURSOR_KIND_WHITE}
};

void draw_cursor(const gui_fb_info_t* fb, int32_t x, int32_t y) {
    uint32_t white = GUI_COLOR_RGB(252, 252, 252);
    uint32_t black = GUI_COLOR_RGB(0, 0, 0);
    if (!fb) return;

    for (uint32_t i = 0; i < CURSOR_POINT_COUNT; i++) {
        int32_t px = x + (int32_t)k_cursor_points[i].dx;
        int32_t py = y + (int32_t)k_cursor_points[i].dy;
        if (!point_in_fb(fb, px, py)) continue;
        uint32_t color = (k_cursor_points[i].kind == CURSOR_KIND_BLACK) ? black : white;
        gui_putpixel((uint32_t)px, (uint32_t)py, color);
    }
}

void cursor_capture_under(const gui_fb_info_t* fb, cursor_save_t* save, int32_t x, int32_t y) {
    if (!fb || !save) return;

    save->x = x;
    save->y = y;
    save->valid = true;

    for (uint32_t i = 0; i < CURSOR_POINT_COUNT; i++) {
        int32_t px = x + (int32_t)k_cursor_points[i].dx;
        int32_t py = y + (int32_t)k_cursor_points[i].dy;
        uint32_t color = DESKTOP_BG;

        if (point_in_fb(fb, px, py)) {
            if (gui_getpixel((uint32_t)px, (uint32_t)py, &color) < 0) {
                color = DESKTOP_BG;
            }
        }
        save->pixels[i] = color;
    }
}

void cursor_restore_under(const gui_fb_info_t* fb, cursor_save_t* save) {
    if (!fb || !save || !save->valid) return;

    for (uint32_t i = 0; i < CURSOR_POINT_COUNT; i++) {
        int32_t px = save->x + (int32_t)k_cursor_points[i].dx;
        int32_t py = save->y + (int32_t)k_cursor_points[i].dy;
        if (!point_in_fb(fb, px, py)) continue;
        gui_putpixel((uint32_t)px, (uint32_t)py, save->pixels[i]);
    }

    save->valid = false;
}
