#include "desktop.h"

static const char* k_icon_labels[WINDOW_COUNT] = { "SHELL", "FILES", "NET" };
static const char* k_title = "SzczupakOS Desktop";
static const char* k_hint = "Tab focus  WASD move  C theme  X close  1-3 toggle  ESC quit";

static void fill_rect_clipped(rect_t target, uint32_t color, rect_t clip) {
    rect_t r = rect_intersection(target, clip);
    if (!rect_valid(r)) return;
    gui_fill_rect((uint32_t)r.x, (uint32_t)r.y, (uint32_t)r.w, (uint32_t)r.h, color);
}

static void draw_frame_clipped(rect_t target, uint32_t color, rect_t clip) {
    if (!rect_valid(target)) return;

    fill_rect_clipped(make_rect(target.x, target.y, target.w, 1), color, clip);
    if (target.h > 1) {
        fill_rect_clipped(make_rect(target.x, target.y + target.h - 1, target.w, 1), color, clip);
    }
    if (target.h > 2) {
        fill_rect_clipped(make_rect(target.x, target.y + 1, 1, target.h - 2), color, clip);
        if (target.w > 1) {
            fill_rect_clipped(make_rect(target.x + target.w - 1, target.y + 1, 1, target.h - 2), color, clip);
        }
    }
}

static void draw_text_if_visible(rect_t clip, int32_t x, int32_t y, const char* text, uint32_t fg, uint32_t bg) {
    if (!text) return;

    int32_t font_w = desktop_font_w();
    int32_t font_h = desktop_font_h();
    int32_t cx = x;
    int32_t cy = y;
    int32_t start_x = x;

    for (const char* p = text; *p; p++) {
        if (*p == '\n') {
            cx = start_x;
            cy += font_h;
            continue;
        }

        rect_t cr = make_rect(cx, cy, font_w, font_h);
        if (rect_intersects(clip, cr)) {
            gui_draw_char((uint32_t)cx, (uint32_t)cy, *p, fg, bg);
        }
        cx += font_w;
    }
}

rect_t expand_repaint_region(const gui_fb_info_t* fb, rect_t region) {
    rect_t out = rect_expand(region, desktop_font_w(), desktop_font_h());
    return clip_to_fb(fb, out);
}

static uint32_t color_scale(uint32_t color, uint32_t percent) {
    uint32_t r = ((color >> 16) & 0xFFu) * percent / 100u;
    uint32_t g = ((color >> 8) & 0xFFu) * percent / 100u;
    uint32_t b = (color & 0xFFu) * percent / 100u;

    if (r > 255u) r = 255u;
    if (g > 255u) g = 255u;
    if (b > 255u) b = 255u;
    return GUI_COLOR_RGB(r, g, b);
}

static void draw_icon(uint32_t x, uint32_t y, const char* label, uint32_t accent, bool visible, bool focused, rect_t clip) {
    uint32_t bg = visible ? ICON_BG : ICON_BG_OFF;
    uint32_t accent_color = visible ? accent : color_scale(accent, 55);
    uint32_t status_color = visible ? GUI_COLOR_RGB(92, 218, 153) : GUI_COLOR_RGB(196, 86, 96);
    uint32_t frame_color = focused ? GUI_COLOR_RGB(245, 250, 255) : accent_color;
    rect_t outer = make_rect((int32_t)x, (int32_t)y, ICON_W, ICON_H);

    fill_rect_clipped(outer, bg, clip);
    draw_frame_clipped(outer, frame_color, clip);
    if (focused) {
        draw_frame_clipped(make_rect((int32_t)x + 2, (int32_t)y + 2, ICON_W - 4, ICON_H - 4), accent_color, clip);
    }
    fill_rect_clipped(make_rect((int32_t)x + 24, (int32_t)y + 16, 36, 36), accent_color, clip);
    fill_rect_clipped(make_rect((int32_t)x + 66, (int32_t)y + 8, 10, 10), status_color, clip);
    draw_text_if_visible(clip, (int32_t)x + 8, (int32_t)y + 60, label, ICON_FG, bg);
}

static void draw_window_contents_region(const desktop_window_t* w, rect_t clip, uint32_t body_color, uint32_t text_color, int32_t title_h) {
    if (!w) return;

    int32_t line_y = w->win.y + 1 + title_h + 10;
    int32_t line_x = w->win.x + 14;
    for (uint32_t i = 0; i < w->line_count; i++) {
        const char* line = w->lines[i] ? w->lines[i] : "";
        draw_text_if_visible(clip, line_x, line_y, line, text_color, body_color);
        line_y += desktop_line_advance();
    }
}

static void draw_window_region(const desktop_window_t* w, bool focused, rect_t clip) {
    if (!w || !w->visible) return;
    if (w->win.width < 4 || w->win.height < 4) return;

    rect_t wr = window_rect(&w->win);
    if (!rect_intersects(clip, wr)) return;

    uint32_t title_h = window_title_height(&w->win);
    uint32_t border = focused ? w->win.border_color : color_scale(w->win.border_color, 60);
    uint32_t title_bg = focused ? w->win.title_bg_color : color_scale(w->win.title_bg_color, 58);
    uint32_t body_bg = focused ? w->win.body_color : color_scale(w->win.body_color, 92);
    uint32_t title_fg = focused ? w->win.title_fg_color : GUI_COLOR_RGB(204, 215, 230);
    uint32_t body_fg = focused ? WINDOW_TEXT_FG : color_scale(WINDOW_TEXT_FG, 75);
    rect_t body_rect = window_body_rect(w);

    fill_rect_clipped(wr, border, clip);
    fill_rect_clipped(make_rect(w->win.x + 1, w->win.y + 1, (int32_t)w->win.width - 2, (int32_t)title_h), title_bg, clip);

    if (w->win.height > title_h + 2) {
        fill_rect_clipped(body_rect, body_bg, clip);
    }

    if (w->win.title) {
        draw_text_if_visible(clip, w->win.x + 8, w->win.y + 4, w->win.title, title_fg, title_bg);
    }

    rect_t close_rect = close_button_rect(&w->win);
    if (rect_intersects(clip, close_rect)) {
        uint32_t close_bg = focused ? GUI_COLOR_RGB(195, 62, 79) : GUI_COLOR_RGB(137, 66, 78);
        uint32_t close_frame = focused ? GUI_COLOR_RGB(246, 191, 198) : GUI_COLOR_RGB(196, 152, 161);
        fill_rect_clipped(close_rect, close_bg, clip);
        draw_frame_clipped(close_rect, close_frame, clip);
    }

    draw_window_contents_region(w, rect_intersection(clip, body_rect), body_bg, body_fg, (int32_t)title_h);
}

static void draw_desktop_region(const gui_fb_info_t* fb,
                                rect_t clip,
                                const desktop_window_t* windows,
                                uint32_t window_count,
                                int32_t active_idx) {
    rect_t c = clip_to_fb(fb, clip);
    if (!rect_valid(c)) return;

    gui_fill_rect((uint32_t)c.x, (uint32_t)c.y, (uint32_t)c.w, (uint32_t)c.h, DESKTOP_BG);
    fill_rect_clipped(desktop_top_rect(fb), DESKTOP_BAR_BG, c);
    fill_rect_clipped(desktop_side_rect(fb), DESKTOP_SIDE_BG, c);

    int32_t right_hint_x = 10;
    int32_t hint_w = (int32_t)strlen(k_hint) * desktop_font_w();
    if ((int32_t)fb->width > hint_w + 10) {
        right_hint_x = (int32_t)fb->width - hint_w - 10;
    }

    const char* active_title = "Desktop";
    if (active_idx >= 0 && (uint32_t)active_idx < window_count && windows[active_idx].visible && windows[active_idx].win.title) {
        active_title = windows[active_idx].win.title;
    }

    static const char* k_open_labels[] = { "Open:0", "Open:1", "Open:2", "Open:3" };
    uint32_t open_count = count_visible_windows(windows, window_count);
    if (open_count > 3) open_count = 3;

    draw_text_if_visible(c, 10, 8, k_title, DESKTOP_BAR_FG, DESKTOP_BAR_BG);
    draw_text_if_visible(c, 184, 8, k_open_labels[open_count], DESKTOP_BAR_FG, DESKTOP_BAR_BG);
    draw_text_if_visible(c, 248, 8, "Focus:", DESKTOP_BAR_FG, DESKTOP_BAR_BG);
    draw_text_if_visible(c, 304, 8, active_title, DESKTOP_BAR_FG, DESKTOP_BAR_BG);
    draw_text_if_visible(c, right_hint_x, 8, k_hint, DESKTOP_BAR_FG, DESKTOP_BAR_BG);

    static const uint32_t k_icon_colors[WINDOW_COUNT] = {
        GUI_COLOR_RGB(88, 186, 158),
        GUI_COLOR_RGB(228, 158, 83),
        GUI_COLOR_RGB(104, 151, 232)
    };

    for (uint32_t i = 0; i < window_count; i++) {
        rect_t ir = icon_rect(i);
        if (!rect_intersects(c, ir)) continue;
        draw_icon((uint32_t)ir.x, (uint32_t)ir.y, k_icon_labels[i], k_icon_colors[i], windows[i].visible, (int32_t)i == active_idx, c);
    }
}

void repaint_region(const gui_fb_info_t* fb,
                   const desktop_window_t* windows,
                   uint32_t window_count,
                   const int32_t* z_order,
                   int32_t active_idx,
                   rect_t region) {
    rect_t c = clip_to_fb(fb, region);
    if (!rect_valid(c)) return;

    draw_desktop_region(fb, c, windows, window_count, active_idx);
    for (uint32_t i = 0; i < window_count; i++) {
        int32_t idx = z_order[i];
        if (idx < 0 || (uint32_t)idx >= window_count) continue;
        draw_window_region(&windows[idx], idx == active_idx, c);
    }
}
