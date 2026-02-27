#include "desktop.h"

static const theme_t themes[] = {
    { GUI_COLOR_RGB(45, 70, 102), GUI_COLOR_RGB(59, 100, 146), GUI_COLOR_RGB(224, 233, 244) },
    { GUI_COLOR_RGB(71, 52, 103), GUI_COLOR_RGB(106, 72, 146), GUI_COLOR_RGB(230, 224, 242) },
    { GUI_COLOR_RGB(66, 72, 42), GUI_COLOR_RGB(102, 114, 66), GUI_COLOR_RGB(234, 238, 221) },
    { GUI_COLOR_RGB(103, 58, 49), GUI_COLOR_RGB(143, 84, 71), GUI_COLOR_RGB(244, 230, 225) }
};

uint32_t desktop_theme_count(void) {
    return (uint32_t)(sizeof(themes) / sizeof(themes[0]));
}

void window_apply_theme(desktop_window_t* w) {
    if (!w) return;

    uint32_t theme_count = desktop_theme_count();
    if (theme_count == 0) return;
    w->theme_idx %= theme_count;
    w->win.border_color = themes[w->theme_idx].border;
    w->win.title_bg_color = themes[w->theme_idx].title_bg;
    w->win.body_color = themes[w->theme_idx].body_bg;
    w->win.title_fg_color = GUI_COLOR_RGB(245, 248, 253);
    w->selected_bg_color = GUI_COLOR_RGB(52, 96, 141);
    w->selected_fg_color = GUI_COLOR_RGB(246, 250, 255);
}

uint32_t window_title_height(const gui_window_t* win) {
    uint32_t title_h = (uint32_t)(desktop_font_h() + 6);
    if (title_h + 2 > win->height) {
        title_h = (win->height > 2) ? (win->height - 2) : 0;
    }
    return title_h;
}

rect_t window_rect(const gui_window_t* win) {
    return make_rect(win->x, win->y, (int32_t)win->width, (int32_t)win->height);
}

rect_t close_button_rect(const gui_window_t* win) {
    uint32_t title_h = window_title_height(win);
    if (win->width < 38 || title_h < 12) return make_rect(0, 0, 0, 0);
    return make_rect(win->x + (int32_t)win->width - 16, win->y + 4, 10, 10);
}

rect_t window_body_rect(const desktop_window_t* w) {
    if (!w) return make_rect(0, 0, 0, 0);
    int32_t title_h = (int32_t)window_title_height(&w->win);
    return make_rect(w->win.x + 1, w->win.y + 1 + title_h, (int32_t)w->win.width - 2, (int32_t)w->win.height - title_h - 2);
}

bool window_geometry_changed(const desktop_window_t* a, const desktop_window_t* b) {
    if (!a || !b) return false;
    return a->win.x != b->win.x || a->win.y != b->win.y || a->win.width != b->win.width || a->win.height != b->win.height;
}

bool window_style_changed(const desktop_window_t* a, const desktop_window_t* b) {
    if (!a || !b) return false;
    return a->theme_idx != b->theme_idx;
}

void dirty_init(dirty_set_t* d) {
    if (!d) return;
    d->count = 0;
    d->full_redraw = false;
}

void dirty_add(dirty_set_t* d, const gui_fb_info_t* fb, rect_t r) {
    if (!d || !fb || d->full_redraw) return;

    rect_t merged = clip_to_fb(fb, r);
    if (!rect_valid(merged)) return;

    for (uint32_t i = 0; i < d->count;) {
        if (!rect_intersects(d->rects[i], merged)) {
            i++;
            continue;
        }
        merged = rect_union(d->rects[i], merged);
        d->rects[i] = d->rects[d->count - 1];
        d->count--;
        i = 0;
    }

    if (d->count < MAX_DIRTY_RECTS) {
        d->rects[d->count++] = merged;
        return;
    }

    d->full_redraw = true;
    d->count = 0;
}

static void dirty_add_window_text(dirty_set_t* d, const gui_fb_info_t* fb, const desktop_window_t* w) {
    if (!d || !fb || !w || !w->visible) return;

    uint32_t title_h = window_title_height(&w->win);
    if (w->win.title) {
        rect_t title_rect = make_rect(w->win.x + 8,
                                      w->win.y + 4,
                                      (int32_t)strlen(w->win.title) * desktop_font_w(),
                                      desktop_font_h());
        rect_t title_clip = make_rect(w->win.x + 1, w->win.y + 1, (int32_t)w->win.width - 2, (int32_t)title_h);
        dirty_add(d, fb, rect_intersection(title_rect, title_clip));
    }

    rect_t body = window_body_rect(w);
    if (!rect_valid(body)) return;

    int32_t line_y = w->win.y + 1 + (int32_t)title_h + 10;
    int32_t line_x = w->win.x + 14;
    for (uint32_t i = 0; i < w->line_count; i++) {
        const char* line = w->lines[i] ? w->lines[i] : "";
        rect_t line_rect = make_rect(line_x,
                                     line_y,
                                     (int32_t)strlen(line) * desktop_font_w(),
                                     desktop_font_h());
        dirty_add(d, fb, rect_intersection(line_rect, body));
        line_y += desktop_line_advance();
    }
}

void dirty_add_all_window_text(dirty_set_t* d, const gui_fb_info_t* fb, const desktop_window_t* windows, uint32_t count) {
    if (!windows) return;
    for (uint32_t i = 0; i < count; i++) {
        dirty_add_window_text(d, fb, &windows[i]);
    }
}

uint32_t count_visible_windows(const desktop_window_t* windows, uint32_t count) {
    uint32_t visible = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (windows[i].visible) visible++;
    }
    return visible;
}

int32_t top_visible_window(const desktop_window_t* windows, const int32_t* z_order, uint32_t count) {
    for (uint32_t i = count; i > 0; i--) {
        int32_t idx = z_order[i - 1];
        if (idx >= 0 && (uint32_t)idx < count && windows[idx].visible) return idx;
    }
    return -1;
}

bool bring_to_front(int32_t* z_order, uint32_t count, int32_t idx) {
    int32_t pos = -1;
    for (uint32_t i = 0; i < count; i++) {
        if (z_order[i] == idx) {
            pos = (int32_t)i;
            break;
        }
    }
    if (pos < 0 || pos == (int32_t)(count - 1)) return false;

    for (uint32_t i = (uint32_t)pos; i + 1 < count; i++) {
        z_order[i] = z_order[i + 1];
    }
    z_order[count - 1] = idx;
    return true;
}

int32_t cycle_focus_window(const desktop_window_t* windows, const int32_t* z_order, uint32_t count, int32_t active_idx) {
    if (count_visible_windows(windows, count) == 0) return -1;
    if (active_idx < 0 || (uint32_t)active_idx >= count || !windows[active_idx].visible) {
        return top_visible_window(windows, z_order, count);
    }

    int32_t active_pos = -1;
    for (uint32_t i = 0; i < count; i++) {
        if (z_order[i] == active_idx) {
            active_pos = (int32_t)i;
            break;
        }
    }
    if (active_pos < 0) return top_visible_window(windows, z_order, count);

    for (uint32_t step = 1; step <= count; step++) {
        uint32_t pos = ((uint32_t)active_pos + step) % count;
        int32_t idx = z_order[pos];
        if (idx >= 0 && (uint32_t)idx < count && windows[idx].visible) return idx;
    }

    return top_visible_window(windows, z_order, count);
}

int32_t hit_test_icon(uint32_t count, int32_t x, int32_t y) {
    for (uint32_t i = 0; i < count; i++) {
        if (point_in_rect(x, y, icon_rect(i))) return (int32_t)i;
    }
    return -1;
}

int32_t hit_test_window(const desktop_window_t* windows, const int32_t* z_order, uint32_t count, int32_t x, int32_t y) {
    for (uint32_t i = count; i > 0; i--) {
        int32_t idx = z_order[i - 1];
        if (idx < 0 || (uint32_t)idx >= count) continue;
        if (!windows[idx].visible) continue;
        if (point_in_rect(x, y, window_rect(&windows[idx].win))) return idx;
    }
    return -1;
}

bool window_title_hit(const desktop_window_t* w, int32_t x, int32_t y) {
    if (!w || !w->visible) return false;
    uint32_t title_h = window_title_height(&w->win);
    rect_t title = make_rect(w->win.x, w->win.y, (int32_t)w->win.width, (int32_t)title_h + 1);
    return point_in_rect(x, y, title);
}

bool window_close_hit(const desktop_window_t* w, int32_t x, int32_t y) {
    if (!w || !w->visible) return false;
    return point_in_rect(x, y, close_button_rect(&w->win));
}

bool clamp_window(desktop_window_t* w, const gui_fb_info_t* fb) {
    if (!w || !fb) return false;

    int32_t old_x = w->win.x;
    int32_t old_y = w->win.y;

    int32_t min_x = DESKTOP_SIDE_W + 20;
    int32_t min_y = DESKTOP_TOP_H + 16;
    int32_t max_x = (int32_t)fb->width - (int32_t)w->win.width - 10;
    int32_t max_y = (int32_t)fb->height - (int32_t)w->win.height - 10;

    if (max_x < min_x) max_x = min_x;
    if (max_y < min_y) max_y = min_y;

    w->win.x = clamp_i32(w->win.x, min_x, max_x);
    w->win.y = clamp_i32(w->win.y, min_y, max_y);
    return old_x != w->win.x || old_y != w->win.y;
}
