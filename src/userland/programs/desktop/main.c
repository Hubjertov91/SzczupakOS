#include "desktop.h"

static const char* k_window1_lines[] = {
    "Mock explorer:",
    " /",
    "  |- bin",
    "  |- etc",
    "  `- home"
};

static const char* k_window2_lines[] = {
    "Network panel:",
    " - rtl8139 initialized",
    " - DHCP + static fallback",
    " - tools: ping / trace / tcp"
};

enum { WINDOW_MOVE_STEP = 12 };

static bool is_valid_window_idx(int32_t idx) {
    return idx >= 0 && (uint32_t)idx < WINDOW_COUNT;
}

static void sync_shell_window(desktop_shell_t* shell_state, desktop_window_t* windows) {
    shell_sync_window(shell_state, &windows[SHELL_WINDOW_IDX]);
}

static void focus_window(int32_t idx, int32_t* active_idx, int32_t* z_order, bool* scene_changed) {
    if (!is_valid_window_idx(idx) || !active_idx || !z_order || !scene_changed) return;

    if (*active_idx != idx) {
        *active_idx = idx;
        *scene_changed = true;
    }
    if (bring_to_front(z_order, WINDOW_COUNT, idx)) {
        *scene_changed = true;
    }
}

static int32_t toggle_key_to_window_idx(char c) {
    if (c < '1' || c > '9') return -1;
    int32_t idx = c - '1';
    return is_valid_window_idx(idx) ? idx : -1;
}

static void init_z_order(int32_t* z_order) {
    if (!z_order) return;
    for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
        z_order[i] = (int32_t)i;
    }
    (void)bring_to_front(z_order, WINDOW_COUNT, SHELL_WINDOW_IDX);
}

int desktop_main(void) {
    gui_fb_info_t fb;
    if (gui_get_fb_info(&fb) < 0) {
        printf("desktop: framebuffer unavailable\n");
        return 1;
    }

    desktop_window_t windows[WINDOW_COUNT] = {
        {
            .win = {.x = 220, .y = 86, .width = 500, .height = 286, .title = "Shell.elf"},
            .theme_idx = 0,
            .lines = NULL,
            .line_count = 0,
            .visible = true
        },
        {
            .win = {.x = 360, .y = 170, .width = 390, .height = 230, .title = "Files"},
            .theme_idx = 1,
            .lines = k_window1_lines,
            .line_count = (uint32_t)(sizeof(k_window1_lines) / sizeof(k_window1_lines[0])),
            .visible = true
        },
        {
            .win = {.x = 470, .y = 116, .width = 400, .height = 220, .title = "Network"},
            .theme_idx = 2,
            .lines = k_window2_lines,
            .line_count = (uint32_t)(sizeof(k_window2_lines) / sizeof(k_window2_lines[0])),
            .visible = true
        }
    };
    desktop_shell_t shell_state;
    shell_init(&shell_state);
    uint32_t theme_count = desktop_theme_count();
    if (theme_count == 0) theme_count = 1;

    int32_t z_order[WINDOW_COUNT];
    init_z_order(z_order);
    int32_t active_idx = SHELL_WINDOW_IDX;

    for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
        window_apply_theme(&windows[i]);
        clamp_window(&windows[i], &fb);
    }
    sync_shell_window(&shell_state, windows);

    int32_t cursor_x = (int32_t)(fb.width / 2);
    int32_t cursor_y = (int32_t)(fb.height / 2);

    int32_t dragging_idx = -1;
    int32_t drag_off_x = 0;
    int32_t drag_off_y = 0;
    cursor_save_t cursor_save = {0};

    repaint_region(&fb, windows, WINDOW_COUNT, z_order, active_idx, make_rect(0, 0, (int32_t)fb.width, (int32_t)fb.height));
    cursor_capture_under(&fb, &cursor_save, cursor_x, cursor_y);
    draw_cursor(&fb, cursor_x, cursor_y);

    bool quit = false;
    while (!quit) {
        desktop_window_t prev_windows[WINDOW_COUNT];
        int32_t prev_z_order[WINDOW_COUNT];
        memcpy(prev_windows, windows, sizeof(windows));
        memcpy(prev_z_order, z_order, sizeof(z_order));

        int32_t prev_active = active_idx;

        bool scene_changed = false;
        bool cursor_changed = false;

        while (1) {
            long key = sys_kb_poll();
            if (key <= 0) break;

            char c = (char)key;
            if ((unsigned char)c == 27u) {
                quit = true;
                break;
            }

            if (c == '\t') {
                int32_t next = cycle_focus_window(windows, z_order, WINDOW_COUNT, active_idx);
                if (!is_valid_window_idx(next)) {
                    if (active_idx != -1) {
                        active_idx = -1;
                        scene_changed = true;
                    }
                } else {
                    focus_window(next, &active_idx, z_order, &scene_changed);
                }
                continue;
            }

            bool shell_focused = (active_idx == SHELL_WINDOW_IDX && windows[SHELL_WINDOW_IDX].visible);
            if (shell_focused) {
                if (c == '\b') {
                    uint32_t old_len = shell_state.input_len;
                    shell_handle_backspace(&shell_state);
                    if (shell_state.input_len != old_len) {
                        sync_shell_window(&shell_state, windows);
                        scene_changed = true;
                    }
                    continue;
                }
                if (c == '\n' || c == '\r') {
                    shell_action_t action = shell_submit_input(&shell_state);
                    if (action == SHELL_ACTION_HIDE_WINDOW) {
                        windows[SHELL_WINDOW_IDX].visible = false;
                        if (dragging_idx == SHELL_WINDOW_IDX) dragging_idx = -1;
                        if (active_idx == SHELL_WINDOW_IDX) {
                            active_idx = top_visible_window(windows, z_order, WINDOW_COUNT);
                        }
                    }
                    sync_shell_window(&shell_state, windows);
                    scene_changed = true;
                    continue;
                }

                uint32_t old_len = shell_state.input_len;
                shell_handle_char(&shell_state, c);
                if (shell_state.input_len != old_len) {
                    sync_shell_window(&shell_state, windows);
                    scene_changed = true;
                }
                continue;
            }

            if (c == 'q' || c == 'Q') {
                quit = true;
                break;
            }

            int32_t idx = toggle_key_to_window_idx(c);
            if (idx >= 0) {
                windows[idx].visible = !windows[idx].visible;
                if (windows[idx].visible) {
                    if (idx == SHELL_WINDOW_IDX) {
                        sync_shell_window(&shell_state, windows);
                    }
                    focus_window(idx, &active_idx, z_order, &scene_changed);
                } else {
                    if (dragging_idx == idx) dragging_idx = -1;
                    if (active_idx == idx) active_idx = top_visible_window(windows, z_order, WINDOW_COUNT);
                    scene_changed = true;
                }
                scene_changed = true;
                continue;
            }

            if (!is_valid_window_idx(active_idx)) continue;
            if (!windows[active_idx].visible) continue;

            if (c == 'w' || c == 'W') {
                windows[active_idx].win.y -= WINDOW_MOVE_STEP;
                scene_changed = true;
            } else if (c == 's' || c == 'S') {
                windows[active_idx].win.y += WINDOW_MOVE_STEP;
                scene_changed = true;
            } else if (c == 'a' || c == 'A') {
                windows[active_idx].win.x -= WINDOW_MOVE_STEP;
                scene_changed = true;
            } else if (c == 'd' || c == 'D') {
                windows[active_idx].win.x += WINDOW_MOVE_STEP;
                scene_changed = true;
            } else if (c == 'c' || c == 'C') {
                windows[active_idx].theme_idx = (windows[active_idx].theme_idx + 1) % theme_count;
                window_apply_theme(&windows[active_idx]);
                scene_changed = true;
            } else if (c == 'x' || c == 'X') {
                windows[active_idx].visible = false;
                if (dragging_idx == active_idx) dragging_idx = -1;
                active_idx = top_visible_window(windows, z_order, WINDOW_COUNT);
                scene_changed = true;
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
                int32_t icon_idx = hit_test_icon(WINDOW_COUNT, cursor_x, cursor_y);
                if (icon_idx >= 0) {
                    if (!windows[icon_idx].visible) {
                        windows[icon_idx].visible = true;
                        if (icon_idx == SHELL_WINDOW_IDX) {
                            sync_shell_window(&shell_state, windows);
                        }
                        scene_changed = true;
                    }
                    focus_window(icon_idx, &active_idx, z_order, &scene_changed);
                    dragging_idx = -1;
                    continue;
                }

                int32_t win_idx = hit_test_window(windows, z_order, WINDOW_COUNT, cursor_x, cursor_y);
                if (win_idx >= 0) {
                    focus_window(win_idx, &active_idx, z_order, &scene_changed);

                    if (window_close_hit(&windows[win_idx], cursor_x, cursor_y)) {
                        windows[win_idx].visible = false;
                        if (dragging_idx == win_idx) dragging_idx = -1;
                        if (active_idx == win_idx) active_idx = top_visible_window(windows, z_order, WINDOW_COUNT);
                        scene_changed = true;
                        continue;
                    }

                    if (window_title_hit(&windows[win_idx], cursor_x, cursor_y)) {
                        dragging_idx = win_idx;
                        drag_off_x = cursor_x - windows[win_idx].win.x;
                        drag_off_y = cursor_y - windows[win_idx].win.y;
                    } else {
                        dragging_idx = -1;
                    }
                } else {
                    if (active_idx != -1) {
                        active_idx = -1;
                        scene_changed = true;
                    }
                    dragging_idx = -1;
                }
            }

            if (left_released) {
                dragging_idx = -1;
            }

            if (is_valid_window_idx(dragging_idx) && left_now && windows[dragging_idx].visible) {
                int32_t nx = cursor_x - drag_off_x;
                int32_t ny = cursor_y - drag_off_y;
                if (nx != windows[dragging_idx].win.x || ny != windows[dragging_idx].win.y) {
                    windows[dragging_idx].win.x = nx;
                    windows[dragging_idx].win.y = ny;
                    scene_changed = true;
                }
            }
        }

        for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
            if (!windows[i].visible) continue;
            if (clamp_window(&windows[i], &fb)) {
                scene_changed = true;
            }
        }

        if (is_valid_window_idx(active_idx) && !windows[active_idx].visible) {
            active_idx = top_visible_window(windows, z_order, WINDOW_COUNT);
            scene_changed = true;
        } else if (!is_valid_window_idx(active_idx) && active_idx != -1) {
            active_idx = top_visible_window(windows, z_order, WINDOW_COUNT);
            scene_changed = true;
        }

        bool z_order_changed = memcmp(prev_z_order, z_order, (long)sizeof(z_order)) != 0;
        uint32_t prev_open_count = count_visible_windows(prev_windows, WINDOW_COUNT);
        uint32_t curr_open_count = count_visible_windows(windows, WINDOW_COUNT);

        dirty_set_t dirty;
        dirty_init(&dirty);

        for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
            bool was_visible = prev_windows[i].visible;
            bool is_visible = windows[i].visible;

            if (was_visible && !is_visible) {
                dirty_add(&dirty, &fb, window_rect(&prev_windows[i].win));
            } else if (!was_visible && is_visible) {
                dirty_add(&dirty, &fb, window_rect(&windows[i].win));
            } else if (was_visible && is_visible) {
                bool moved = window_geometry_changed(&prev_windows[i], &windows[i]);
                bool style_changed = window_style_changed(&prev_windows[i], &windows[i]);
                bool focus_style_changed = ((int32_t)i == prev_active) != ((int32_t)i == active_idx);

                if (moved) {
                    dirty_add(&dirty, &fb, window_rect(&prev_windows[i].win));
                    dirty_add(&dirty, &fb, window_rect(&windows[i].win));
                } else if (style_changed || focus_style_changed) {
                    dirty_add(&dirty, &fb, window_rect(&windows[i].win));
                }
            }
        }

        if (z_order_changed) {
            for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
                if (prev_windows[i].visible) dirty_add(&dirty, &fb, window_rect(&prev_windows[i].win));
                if (windows[i].visible) dirty_add(&dirty, &fb, window_rect(&windows[i].win));
            }
        }

        if (prev_open_count != curr_open_count || prev_active != active_idx) {
            dirty_add(&dirty, &fb, desktop_top_rect(&fb));
        }

        for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
            bool vis_changed = prev_windows[i].visible != windows[i].visible;
            bool focus_changed = ((int32_t)i == prev_active) != ((int32_t)i == active_idx);
            if (vis_changed || focus_changed) {
                dirty_add(&dirty, &fb, icon_rect(i));
            }
        }

        if (scene_changed) {
            // `prev_windows` is a shallow copy, so line pointers can alias the
            // current shell view buffer. Redrawing full bodies avoids stale text
            // artifacts when line lengths/shifts change after typing.
            for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
                if (prev_windows[i].visible) dirty_add(&dirty, &fb, window_body_rect(&prev_windows[i]));
                if (windows[i].visible) dirty_add(&dirty, &fb, window_body_rect(&windows[i]));
            }
        }

        if (is_valid_window_idx(dragging_idx) && scene_changed) {
            dirty.full_redraw = true;
            dirty.count = 0;
        }

        if (scene_changed && !dirty.full_redraw && dirty.count == 0) {
            dirty_add(&dirty, &fb, make_rect(0, 0, (int32_t)fb.width, (int32_t)fb.height));
        }

        bool cursor_overlay_needs_update = cursor_changed || scene_changed;
        if (cursor_overlay_needs_update) {
            cursor_restore_under(&fb, &cursor_save);
        }

        if (dirty.full_redraw) {
            repaint_region(&fb, windows, WINDOW_COUNT, z_order, active_idx, make_rect(0, 0, (int32_t)fb.width, (int32_t)fb.height));
        } else {
            for (uint32_t i = 0; i < dirty.count; i++) {
                rect_t paint = expand_repaint_region(&fb, dirty.rects[i]);
                repaint_region(&fb, windows, WINDOW_COUNT, z_order, active_idx, paint);
            }
        }

        if (cursor_overlay_needs_update) {
            cursor_capture_under(&fb, &cursor_save, cursor_x, cursor_y);
            draw_cursor(&fb, cursor_x, cursor_y);
        }

        uint32_t frame_ms = 16;
        if (cursor_changed && !scene_changed) frame_ms = 8;
        sys_sleep((long)frame_ms);
    }

    sys_clear();
    printf("desktop: closed\n");
    return 0;
}
