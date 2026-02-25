#include <gui.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>

#define DESKTOP_BG      GUI_COLOR_RGB(23, 53, 84)
#define DESKTOP_BAR_BG  GUI_COLOR_RGB(15, 30, 49)
#define DESKTOP_BAR_FG  GUI_COLOR_RGB(218, 231, 243)
#define DESKTOP_SIDE_BG GUI_COLOR_RGB(30, 66, 102)
#define ICON_BG         GUI_COLOR_RGB(44, 88, 133)
#define ICON_FG         GUI_COLOR_RGB(237, 244, 252)
#define WINDOW_TEXT_FG  GUI_COLOR_RGB(33, 44, 58)

#define CURSOR_HALF 6
#define CURSOR_SIZE (CURSOR_HALF * 2 + 1)

typedef struct {
    uint32_t border;
    uint32_t title_bg;
    uint32_t body_bg;
} theme_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} rect_t;

static const theme_t themes[] = {
    { GUI_COLOR_RGB(45, 70, 102), GUI_COLOR_RGB(59, 100, 146), GUI_COLOR_RGB(224, 233, 244) },
    { GUI_COLOR_RGB(71, 52, 103), GUI_COLOR_RGB(106, 72, 146), GUI_COLOR_RGB(230, 224, 242) },
    { GUI_COLOR_RGB(66, 72, 42), GUI_COLOR_RGB(102, 114, 66), GUI_COLOR_RGB(234, 238, 221) }
};

static const char* k_title = "SzczupakOS Desktop Prototype";
static const char* k_hint = "WASD move  C recolor  LMB drag  Q quit";

static int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static rect_t make_rect(int32_t x, int32_t y, int32_t w, int32_t h) {
    rect_t r;
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

static bool rect_valid(rect_t r) {
    return r.w > 0 && r.h > 0;
}

static bool rect_intersects(rect_t a, rect_t b) {
    if (!rect_valid(a) || !rect_valid(b)) return false;
    if (a.x + a.w <= b.x) return false;
    if (b.x + b.w <= a.x) return false;
    if (a.y + a.h <= b.y) return false;
    if (b.y + b.h <= a.y) return false;
    return true;
}

static rect_t rect_intersection(rect_t a, rect_t b) {
    rect_t out;
    int32_t x1 = (a.x > b.x) ? a.x : b.x;
    int32_t y1 = (a.y > b.y) ? a.y : b.y;
    int32_t x2 = (a.x + a.w < b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int32_t y2 = (a.y + a.h < b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    out.x = x1;
    out.y = y1;
    out.w = x2 - x1;
    out.h = y2 - y1;
    if (out.w < 0) out.w = 0;
    if (out.h < 0) out.h = 0;
    return out;
}

static rect_t rect_union(rect_t a, rect_t b) {
    if (!rect_valid(a)) return b;
    if (!rect_valid(b)) return a;

    int32_t x1 = (a.x < b.x) ? a.x : b.x;
    int32_t y1 = (a.y < b.y) ? a.y : b.y;
    int32_t x2 = (a.x + a.w > b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
    int32_t y2 = (a.y + a.h > b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
    return make_rect(x1, y1, x2 - x1, y2 - y1);
}

static rect_t clip_to_fb(const gui_fb_info_t* fb, rect_t r) {
    rect_t fb_rect = make_rect(0, 0, (int32_t)fb->width, (int32_t)fb->height);
    return rect_intersection(r, fb_rect);
}

static void fill_rect_clipped(rect_t target, uint32_t color, rect_t clip) {
    rect_t r = rect_intersection(target, clip);
    if (!rect_valid(r)) return;
    gui_fill_rect((uint32_t)r.x, (uint32_t)r.y, (uint32_t)r.w, (uint32_t)r.h, color);
}

static void draw_text_if_visible(rect_t clip, int32_t x, int32_t y, const char* text, uint32_t fg, uint32_t bg) {
    rect_t tr = make_rect(x, y, (int32_t)strlen(text) * (int32_t)GUI_FONT_WIDTH, (int32_t)GUI_FONT_HEIGHT);
    if (!rect_intersects(clip, tr)) return;
    gui_draw_text((uint32_t)x, (uint32_t)y, text, fg, bg);
}

static void draw_icon(uint32_t x, uint32_t y, const char* label, uint32_t accent) {
    gui_fill_rect(x, y, 84, 84, ICON_BG);
    gui_draw_frame(x, y, 84, 84, accent);
    gui_fill_rect(x + 24, y + 16, 36, 36, accent);
    gui_draw_text(x + 12, y + 60, label, ICON_FG, ICON_BG);
}

static uint32_t window_title_height(const gui_window_t* win) {
    uint32_t title_h = GUI_FONT_HEIGHT + 6;
    if (title_h + 2 > win->height) title_h = win->height - 2;
    return title_h;
}

static rect_t window_rect(const gui_window_t* win) {
    return make_rect(win->x, win->y, (int32_t)win->width, (int32_t)win->height);
}

static rect_t cursor_rect(int32_t x, int32_t y) {
    return make_rect(x - CURSOR_HALF, y - CURSOR_HALF, CURSOR_SIZE, CURSOR_SIZE);
}

static void draw_window_contents_region(const gui_window_t* win, rect_t clip) {
    if (!win) return;
    if (win->x < 0 || win->y < 0) return;

    int32_t x = win->x;
    int32_t y = win->y;

    draw_text_if_visible(clip, x + 14, y + 34, "Pierwszy etap GUI dziala.", WINDOW_TEXT_FG, win->body_color);
    draw_text_if_visible(clip, x + 14, y + 54, "Teraz mamy:", WINDOW_TEXT_FG, win->body_color);
    draw_text_if_visible(clip, x + 24, y + 74, "- desktop", WINDOW_TEXT_FG, win->body_color);
    draw_text_if_visible(clip, x + 24, y + 92, "- ikony", WINDOW_TEXT_FG, win->body_color);
    draw_text_if_visible(clip, x + 24, y + 110, "- przesuwalne okno", WINDOW_TEXT_FG, win->body_color);
    draw_text_if_visible(clip, x + 14, y + 136, "Nastepny krok: mysz + focus + wiele okien.", WINDOW_TEXT_FG, win->body_color);
}

static void draw_window_region(const gui_window_t* win, rect_t clip) {
    if (!win) return;
    if (win->x < 0 || win->y < 0) return;
    if (win->width < 4 || win->height < 4) return;

    rect_t wr = window_rect(win);
    if (!rect_intersects(clip, wr)) return;

    uint32_t title_h = window_title_height(win);

    fill_rect_clipped(wr, win->border_color, clip);
    fill_rect_clipped(make_rect(win->x + 1, win->y + 1, (int32_t)win->width - 2, (int32_t)title_h),
                      win->title_bg_color, clip);

    if (win->height > title_h + 2) {
        fill_rect_clipped(
            make_rect(win->x + 1, win->y + 1 + (int32_t)title_h, (int32_t)win->width - 2, (int32_t)win->height - (int32_t)title_h - 2),
            win->body_color, clip);
    }

    if (win->title) {
        draw_text_if_visible(clip, win->x + 8, win->y + 4, win->title, win->title_fg_color, win->title_bg_color);
    }

    if (win->width >= 38 && title_h >= 12) {
        rect_t close_rect = make_rect(win->x + (int32_t)win->width - 16, win->y + 4, 10, 10);
        if (rect_intersects(clip, close_rect)) {
            gui_fill_rect((uint32_t)close_rect.x, (uint32_t)close_rect.y, (uint32_t)close_rect.w, (uint32_t)close_rect.h,
                          GUI_COLOR_RGB(195, 62, 79));
            gui_draw_frame((uint32_t)close_rect.x, (uint32_t)close_rect.y, (uint32_t)close_rect.w, (uint32_t)close_rect.h,
                           GUI_COLOR_RGB(246, 191, 198));
        }
    }

    draw_window_contents_region(win, clip);
}

static void draw_desktop_region(const gui_fb_info_t* fb, rect_t clip) {
    rect_t c = clip_to_fb(fb, clip);
    if (!rect_valid(c)) return;

    gui_fill_rect((uint32_t)c.x, (uint32_t)c.y, (uint32_t)c.w, (uint32_t)c.h, DESKTOP_BG);

    fill_rect_clipped(make_rect(0, 0, (int32_t)fb->width, 32), DESKTOP_BAR_BG, c);
    if (fb->height > 32) {
        fill_rect_clipped(make_rect(0, 32, 150, (int32_t)fb->height - 32), DESKTOP_SIDE_BG, c);
    }

    int32_t right_hint_x = 10;
    int32_t hint_w = (int32_t)strlen(k_hint) * (int32_t)GUI_FONT_WIDTH;
    if ((int32_t)fb->width > hint_w + 10) {
        right_hint_x = (int32_t)fb->width - hint_w - 10;
    }

    draw_text_if_visible(c, 10, 8, k_title, DESKTOP_BAR_FG, DESKTOP_BAR_BG);
    draw_text_if_visible(c, right_hint_x, 8, k_hint, DESKTOP_BAR_FG, DESKTOP_BAR_BG);

    rect_t icon1 = make_rect(20, 52, 84, 84);
    rect_t icon2 = make_rect(20, 156, 84, 84);
    rect_t icon3 = make_rect(20, 260, 84, 84);

    if (rect_intersects(c, icon1)) draw_icon(20, 52, "SHELL", GUI_COLOR_RGB(88, 186, 158));
    if (rect_intersects(c, icon2)) draw_icon(20, 156, "FILES", GUI_COLOR_RGB(228, 158, 83));
    if (rect_intersects(c, icon3)) draw_icon(20, 260, "NET", GUI_COLOR_RGB(104, 151, 232));
}

static void repaint_region(const gui_fb_info_t* fb, const gui_window_t* win, rect_t region) {
    rect_t c = clip_to_fb(fb, region);
    if (!rect_valid(c)) return;
    draw_desktop_region(fb, c);
    draw_window_region(win, c);
}

static void draw_cursor(const gui_fb_info_t* fb, int32_t x, int32_t y) {
    for (int32_t i = -CURSOR_HALF; i <= CURSOR_HALF; i++) {
        int32_t px = x + i;
        int32_t py = y;
        if (px >= 0 && py >= 0 && px < (int32_t)fb->width && py < (int32_t)fb->height) {
            gui_putpixel((uint32_t)px, (uint32_t)py, GUI_COLOR_RGB(252, 252, 252));
        }
    }

    for (int32_t i = -CURSOR_HALF; i <= CURSOR_HALF; i++) {
        int32_t px = x;
        int32_t py = y + i;
        if (px >= 0 && py >= 0 && px < (int32_t)fb->width && py < (int32_t)fb->height) {
            gui_putpixel((uint32_t)px, (uint32_t)py, GUI_COLOR_RGB(252, 252, 252));
        }
    }

    if (x + 1 >= 0 && y + 1 >= 0 && x + 1 < (int32_t)fb->width && y + 1 < (int32_t)fb->height) {
        gui_putpixel((uint32_t)(x + 1), (uint32_t)(y + 1), GUI_COLOR_RGB(0, 0, 0));
    }
}

static void clamp_window(gui_window_t* win, const gui_fb_info_t* fb) {
    if (!win || !fb) return;

    int32_t min_x = 170;
    int32_t min_y = 48;
    int32_t max_x = (int32_t)fb->width - (int32_t)win->width - 10;
    int32_t max_y = (int32_t)fb->height - (int32_t)win->height - 10;

    if (max_x < min_x) max_x = min_x;
    if (max_y < min_y) max_y = min_y;

    win->x = clamp_i32(win->x, min_x, max_x);
    win->y = clamp_i32(win->y, min_y, max_y);
}

int main(void) {
    gui_fb_info_t fb;
    if (gui_get_fb_info(&fb) < 0) {
        printf("desktop: framebuffer unavailable\n");
        return 1;
    }

    uint32_t theme_idx = 0;
    gui_window_t win = {
        .x = 220,
        .y = 96,
        .width = 520,
        .height = 260,
        .border_color = themes[theme_idx].border,
        .title_bg_color = themes[theme_idx].title_bg,
        .title_fg_color = GUI_COLOR_RGB(245, 248, 253),
        .body_color = themes[theme_idx].body_bg,
        .title = "Window 1 - SzczupakOS"
    };

    clamp_window(&win, &fb);

    int32_t cursor_x = (int32_t)(fb.width / 2);
    int32_t cursor_y = (int32_t)(fb.height / 2);

    bool dragging = false;
    int32_t drag_off_x = 0;
    int32_t drag_off_y = 0;

    repaint_region(&fb, &win, make_rect(0, 0, (int32_t)fb.width, (int32_t)fb.height));
    draw_cursor(&fb, cursor_x, cursor_y);

    bool quit = false;
    while (!quit) {
        gui_window_t prev_win = win;
        int32_t prev_cursor_x = cursor_x;
        int32_t prev_cursor_y = cursor_y;

        bool window_changed = false;
        bool cursor_changed = false;
        rect_t dirty = make_rect(0, 0, 0, 0);

        while (1) {
            long key = sys_kb_poll();
            if (key <= 0) break;

            char c = (char)key;
            if (c == 'q' || c == 'Q') {
                quit = true;
                break;
            }

            if (c == 'w' || c == 'W') {
                win.y -= 12;
                window_changed = true;
            } else if (c == 's' || c == 'S') {
                win.y += 12;
                window_changed = true;
            } else if (c == 'a' || c == 'A') {
                win.x -= 12;
                window_changed = true;
            } else if (c == 'd' || c == 'D') {
                win.x += 12;
                window_changed = true;
            } else if (c == 'c' || c == 'C') {
                theme_idx = (theme_idx + 1) % (sizeof(themes) / sizeof(themes[0]));
                win.border_color = themes[theme_idx].border;
                win.title_bg_color = themes[theme_idx].title_bg;
                win.body_color = themes[theme_idx].body_bg;
                window_changed = true;
            }
        }

        if (quit) break;

        while (1) {
            struct mouse_event me;
            long ret = sys_mouse_poll(&me);
            if (ret <= 0) break;

            if (me.x != cursor_x || me.y != cursor_y) {
                cursor_x = me.x;
                cursor_y = me.y;
                cursor_changed = true;
            }

            bool left_now = (me.buttons & 0x1u) != 0;
            bool left_changed = (me.changed & 0x1u) != 0;
            bool left_pressed = left_changed && left_now;
            bool left_released = left_changed && !left_now;

            if (left_pressed) {
                int32_t title_h = (int32_t)window_title_height(&win);
                bool in_x = (cursor_x >= win.x) && (cursor_x < (win.x + (int32_t)win.width));
                bool in_y = (cursor_y >= win.y) && (cursor_y < (win.y + title_h + 1));
                if (in_x && in_y) {
                    dragging = true;
                    drag_off_x = cursor_x - win.x;
                    drag_off_y = cursor_y - win.y;
                }
            }

            if (left_released) {
                dragging = false;
            }

            if (dragging && left_now) {
                int32_t nx = cursor_x - drag_off_x;
                int32_t ny = cursor_y - drag_off_y;
                if (nx != win.x || ny != win.y) {
                    win.x = nx;
                    win.y = ny;
                    window_changed = true;
                }
            }
        }

        if (window_changed) {
            clamp_window(&win, &fb);
            dirty = rect_union(dirty, window_rect(&prev_win));
            dirty = rect_union(dirty, window_rect(&win));
        }

        if (cursor_changed || window_changed) {
            dirty = rect_union(dirty, cursor_rect(prev_cursor_x, prev_cursor_y));
            dirty = rect_union(dirty, cursor_rect(cursor_x, cursor_y));
        }

        if (rect_valid(dirty)) {
            repaint_region(&fb, &win, dirty);
            draw_cursor(&fb, cursor_x, cursor_y);
        }

        sys_sleep(16);
    }

    sys_clear();
    printf("desktop: closed\n");
    return 0;
}
