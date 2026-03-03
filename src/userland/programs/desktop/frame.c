#include "desktop.h"

static bool is_valid_window_idx(int32_t idx) {
    return idx >= 0 && (uint32_t)idx < WINDOW_COUNT;
}

static bool sanitize_z_order(int32_t* z_order) {
    if (!z_order) return false;

    bool seen[WINDOW_COUNT];
    for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
        seen[i] = false;
    }

    for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
        int32_t idx = z_order[i];
        if (idx < 0 || (uint32_t)idx >= WINDOW_COUNT || seen[idx]) {
            for (uint32_t j = 0; j < WINDOW_COUNT; j++) {
                z_order[j] = (int32_t)j;
            }
            return true;
        }
        seen[idx] = true;
    }
    return false;
}

static bool sanitize_runtime_state(const gui_fb_info_t* fb, desktop_runtime_t* rt) {
    if (!fb || !rt) return false;

    bool changed = false;
    if (sanitize_z_order(rt->z_order)) {
        changed = true;
    }

    if (!is_valid_window_idx(rt->active_idx) && rt->active_idx != -1) {
        rt->active_idx = top_visible_window(rt->windows, rt->z_order, WINDOW_COUNT);
        changed = true;
    }
    if (is_valid_window_idx(rt->active_idx) && !rt->windows[rt->active_idx].visible) {
        rt->active_idx = top_visible_window(rt->windows, rt->z_order, WINDOW_COUNT);
        changed = true;
    }

    if (!is_valid_window_idx(rt->dragging_idx) ||
        !rt->windows[rt->dragging_idx].visible) {
        if (rt->dragging_idx != -1) {
            rt->dragging_idx = -1;
            changed = true;
        }
    }

    int32_t max_x = (fb->width > 0u) ? ((int32_t)fb->width - 1) : 0;
    int32_t max_y = (fb->height > 0u) ? ((int32_t)fb->height - 1) : 0;
    int32_t clamped_x = clamp_i32(rt->cursor_x, 0, max_x);
    int32_t clamped_y = clamp_i32(rt->cursor_y, 0, max_y);
    if (clamped_x != rt->cursor_x || clamped_y != rt->cursor_y) {
        rt->cursor_x = clamped_x;
        rt->cursor_y = clamped_y;
        changed = true;
    }

    return changed;
}

void desktop_finalize_state(desktop_runtime_t* rt, const gui_fb_info_t* fb, desktop_frame_state_t* frame) {
    if (!rt || !fb || !frame) return;

    for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
        if (!rt->windows[i].visible) continue;
        if (clamp_window(&rt->windows[i], fb)) {
            frame->scene_changed = true;
        }
    }

    if (is_valid_window_idx(rt->active_idx) && !rt->windows[rt->active_idx].visible) {
        rt->active_idx = top_visible_window(rt->windows, rt->z_order, WINDOW_COUNT);
        frame->scene_changed = true;
    } else if (!is_valid_window_idx(rt->active_idx) && rt->active_idx != -1) {
        rt->active_idx = top_visible_window(rt->windows, rt->z_order, WINDOW_COUNT);
        frame->scene_changed = true;
    }
}

void desktop_render_frame(const gui_fb_info_t* fb,
                          desktop_runtime_t* rt,
                          const desktop_snapshot_t* prev,
                          const desktop_frame_state_t* frame) {
    if (!fb || !rt || !prev || !frame) return;

    bool runtime_repaired = sanitize_runtime_state(fb, rt);

    bool z_order_changed = memcmp(prev->z_order, rt->z_order, (long)sizeof(rt->z_order)) != 0;
    uint32_t prev_open_count = count_visible_windows(prev->windows, WINDOW_COUNT);
    uint32_t curr_open_count = count_visible_windows(rt->windows, WINDOW_COUNT);

    dirty_set_t dirty;
    dirty_init(&dirty);
    if (frame->force_full_redraw || runtime_repaired) {
        dirty.full_redraw = true;
    }

    for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
        bool was_visible = prev->windows[i].visible;
        bool is_visible = rt->windows[i].visible;

        if (was_visible && !is_visible) {
            dirty_add(&dirty, fb, window_rect(&prev->windows[i].win));
        } else if (!was_visible && is_visible) {
            dirty_add(&dirty, fb, window_rect(&rt->windows[i].win));
        } else if (was_visible && is_visible) {
            bool moved = window_geometry_changed(&prev->windows[i], &rt->windows[i]);
            bool style_changed = window_style_changed(&prev->windows[i], &rt->windows[i]);
            bool focus_style_changed = ((int32_t)i == prev->active_idx) != ((int32_t)i == rt->active_idx);

            if (moved) {
                dirty_add(&dirty, fb, window_rect(&prev->windows[i].win));
                dirty_add(&dirty, fb, window_rect(&rt->windows[i].win));
            } else if (style_changed || focus_style_changed) {
                dirty_add(&dirty, fb, window_rect(&rt->windows[i].win));
            }
        }
    }

    if (z_order_changed) {
        for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
            if (prev->windows[i].visible) dirty_add(&dirty, fb, window_rect(&prev->windows[i].win));
            if (rt->windows[i].visible) dirty_add(&dirty, fb, window_rect(&rt->windows[i].win));
        }
    }

    if (prev_open_count != curr_open_count || prev->active_idx != rt->active_idx) {
        dirty_add(&dirty, fb, desktop_top_rect(fb));
    }

    for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
        bool vis_changed = prev->windows[i].visible != rt->windows[i].visible;
        bool focus_changed = ((int32_t)i == prev->active_idx) != ((int32_t)i == rt->active_idx);
        if (vis_changed || focus_changed) {
            dirty_add(&dirty, fb, icon_rect(i));
        }
    }

    if (frame->scene_changed) {
        for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
            if (prev->windows[i].visible) dirty_add(&dirty, fb, window_body_rect(&prev->windows[i]));
            if (rt->windows[i].visible) dirty_add(&dirty, fb, window_body_rect(&rt->windows[i]));
        }
    }

    if (is_valid_window_idx(rt->dragging_idx) && frame->scene_changed) {
        dirty.full_redraw = true;
        dirty.count = 0;
    }

    if (frame->scene_changed && !dirty.full_redraw && dirty.count == 0) {
        dirty_add(&dirty, fb, make_rect(0, 0, (int32_t)fb->width, (int32_t)fb->height));
    }

    bool cursor_overlay_needs_update = frame->cursor_changed || frame->scene_changed;
    if (cursor_overlay_needs_update && !rt->cursor_save.valid) {
        dirty.full_redraw = true;
    }
    if (cursor_overlay_needs_update) {
        cursor_restore_under(fb, &rt->cursor_save);
    }

    if (dirty.full_redraw) {
        repaint_region(fb, rt->windows, WINDOW_COUNT, rt->z_order, rt->active_idx,
                      make_rect(0, 0, (int32_t)fb->width, (int32_t)fb->height));
    } else {
        for (uint32_t i = 0; i < dirty.count; i++) {
            rect_t paint = expand_repaint_region(fb, dirty.rects[i]);
            repaint_region(fb, rt->windows, WINDOW_COUNT, rt->z_order, rt->active_idx, paint);
        }
    }

    if (cursor_overlay_needs_update) {
        cursor_capture_under(fb, &rt->cursor_save, rt->cursor_x, rt->cursor_y);
        draw_cursor(fb, rt->cursor_x, rt->cursor_y);
    }
}

uint32_t desktop_frame_sleep_ms(const desktop_frame_state_t* frame) {
    if (!frame) return 16;
    if (frame->cursor_changed && !frame->scene_changed) return 8;
    return 16;
}
