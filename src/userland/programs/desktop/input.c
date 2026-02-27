#include "desktop.h"

enum { WINDOW_MOVE_STEP = 12 };
enum { FILES_DOUBLE_CLICK_TICKS = 30 };

static bool is_valid_window_idx(int32_t idx) {
    return idx >= 0 && (uint32_t)idx < WINDOW_COUNT;
}

static void sync_shell_window(desktop_runtime_t* rt) {
    shell_sync_window(&rt->shell_state, &rt->windows[SHELL_WINDOW_IDX]);
}

static void apply_shell_action(desktop_runtime_t* rt, desktop_frame_state_t* frame, shell_action_t action) {
    if (!rt || !frame) return;
    if ((action & SHELL_ACTION_HIDE_WINDOW) != 0u) {
        rt->windows[SHELL_WINDOW_IDX].visible = false;
        if (rt->dragging_idx == SHELL_WINDOW_IDX) rt->dragging_idx = -1;
        if (rt->active_idx == SHELL_WINDOW_IDX) {
            rt->active_idx = top_visible_window(rt->windows, rt->z_order, WINDOW_COUNT);
        }
        frame->scene_changed = true;
    }
    if ((action & SHELL_ACTION_FORCE_FULL_REPAINT) != 0u) {
        frame->force_full_redraw = true;
    }
}

static int32_t files_entry_at_point(const desktop_runtime_t* rt, int32_t x, int32_t y) {
    if (!rt) return -1;

    const desktop_window_t* w = &rt->windows[FILES_WINDOW_IDX];
    if (!w->visible) return -1;

    rect_t body = window_body_rect(w);
    if (!point_in_rect(x, y, body)) return -1;

    int32_t line_y0 = w->win.y + 1 + (int32_t)window_title_height(&w->win) + 10;
    int32_t rel = y - line_y0;
    if (rel < 0) return -1;

    int32_t line_idx = rel / desktop_line_advance();
    if (line_idx < 0 || (uint32_t)line_idx >= w->line_count) return -1;

    int32_t entry_idx = rt->files_line_to_entry[line_idx];
    if (entry_idx < 0 || (uint32_t)entry_idx >= rt->files_entry_count) return -1;
    return entry_idx;
}

static void focus_window(desktop_runtime_t* rt, int32_t idx, bool* scene_changed) {
    if (!rt || !scene_changed || !is_valid_window_idx(idx)) return;

    if (rt->active_idx != idx) {
        rt->active_idx = idx;
        *scene_changed = true;
    }
    if (bring_to_front(rt->z_order, WINDOW_COUNT, idx)) {
        *scene_changed = true;
    }
}

static int32_t toggle_key_to_window_idx(char c) {
    if (c < '1' || c > '9') return -1;
    int32_t idx = c - '1';
    return is_valid_window_idx(idx) ? idx : -1;
}

void desktop_poll_keyboard(desktop_runtime_t* rt, desktop_frame_state_t* frame) {
    if (!rt || !frame) return;

    while (1) {
        long key = sys_kb_poll();
        if (key <= 0) break;

        char c = (char)key;
        if ((unsigned char)c == 27u) {
            frame->quit = true;
            break;
        }

        if (c == '\t') {
            int32_t next = cycle_focus_window(rt->windows, rt->z_order, WINDOW_COUNT, rt->active_idx);
            if (!is_valid_window_idx(next)) {
                if (rt->active_idx != -1) {
                    rt->active_idx = -1;
                    frame->scene_changed = true;
                }
            } else {
                focus_window(rt, next, &frame->scene_changed);
            }
            continue;
        }

        bool shell_focused = (rt->active_idx == SHELL_WINDOW_IDX && rt->windows[SHELL_WINDOW_IDX].visible);
        if (shell_focused) {
            if (c == '\b') {
                uint32_t old_len = rt->shell_state.input_len;
                shell_handle_backspace(&rt->shell_state);
                if (rt->shell_state.input_len != old_len) {
                    sync_shell_window(rt);
                    frame->scene_changed = true;
                }
                continue;
            }
            if (c == '\n' || c == '\r') {
                shell_action_t action = shell_submit_input(&rt->shell_state);
                apply_shell_action(rt, frame, action);
                sync_shell_window(rt);
                frame->scene_changed = true;
                continue;
            }

            uint32_t old_len = rt->shell_state.input_len;
            shell_handle_char(&rt->shell_state, c);
            if (rt->shell_state.input_len != old_len) {
                sync_shell_window(rt);
                frame->scene_changed = true;
            }
            continue;
        }

        bool files_focused = (rt->active_idx == FILES_WINDOW_IDX && rt->windows[FILES_WINDOW_IDX].visible);
        if (files_focused) {
            if (c == 'j' || c == 'J') {
                if (desktop_files_select_next(rt, 1)) frame->scene_changed = true;
                continue;
            }
            if (c == 'k' || c == 'K') {
                if (desktop_files_select_next(rt, -1)) frame->scene_changed = true;
                continue;
            }
            if (c == 'u' || c == 'U' || c == '\b') {
                rt->files_selected_entry = 0;
                bool changed = false;
                shell_action_t action = desktop_files_activate_selected(rt, &changed);
                apply_shell_action(rt, frame, action);
                if (changed) frame->scene_changed = true;
                continue;
            }
            if (c == 'o' || c == 'O' || c == '\n' || c == '\r') {
                bool changed = false;
                shell_action_t action = desktop_files_activate_selected(rt, &changed);
                apply_shell_action(rt, frame, action);
                if (changed) frame->scene_changed = true;
                continue;
            }
            if (c == 'd' || c == 'D') {
                if (desktop_files_delete_selected(rt)) frame->scene_changed = true;
                continue;
            }
            if (c == 'r' || c == 'R') {
                if (desktop_refresh_files_panel(rt)) frame->scene_changed = true;
                continue;
            }
        }

        if (c == 'q' || c == 'Q') {
            frame->quit = true;
            break;
        }

        int32_t idx = toggle_key_to_window_idx(c);
        if (idx >= 0) {
            rt->windows[idx].visible = !rt->windows[idx].visible;
            if (rt->windows[idx].visible) {
                if (idx == SHELL_WINDOW_IDX) {
                    sync_shell_window(rt);
                }
                focus_window(rt, idx, &frame->scene_changed);
            } else {
                if (rt->dragging_idx == idx) rt->dragging_idx = -1;
                if (rt->active_idx == idx) rt->active_idx = top_visible_window(rt->windows, rt->z_order, WINDOW_COUNT);
                frame->scene_changed = true;
            }
            frame->scene_changed = true;
            continue;
        }

        if (!is_valid_window_idx(rt->active_idx)) continue;
        if (!rt->windows[rt->active_idx].visible) continue;

        if (c == 'w' || c == 'W') {
            rt->windows[rt->active_idx].win.y -= WINDOW_MOVE_STEP;
            frame->scene_changed = true;
        } else if (c == 's' || c == 'S') {
            rt->windows[rt->active_idx].win.y += WINDOW_MOVE_STEP;
            frame->scene_changed = true;
        } else if (c == 'a' || c == 'A') {
            rt->windows[rt->active_idx].win.x -= WINDOW_MOVE_STEP;
            frame->scene_changed = true;
        } else if (c == 'd' || c == 'D') {
            rt->windows[rt->active_idx].win.x += WINDOW_MOVE_STEP;
            frame->scene_changed = true;
        } else if (c == 'c' || c == 'C') {
            rt->windows[rt->active_idx].theme_idx = (rt->windows[rt->active_idx].theme_idx + 1) % rt->theme_count;
            window_apply_theme(&rt->windows[rt->active_idx]);
            frame->scene_changed = true;
        } else if (c == 'x' || c == 'X') {
            rt->windows[rt->active_idx].visible = false;
            if (rt->dragging_idx == rt->active_idx) rt->dragging_idx = -1;
            rt->active_idx = top_visible_window(rt->windows, rt->z_order, WINDOW_COUNT);
            frame->scene_changed = true;
        }
    }
}

void desktop_poll_mouse(desktop_runtime_t* rt, desktop_frame_state_t* frame) {
    if (!rt || !frame) return;

    while (1) {
        struct mouse_event me;
        long ret = sys_mouse_poll(&me);
        if (ret <= 0) break;

        if (me.x != rt->cursor_x || me.y != rt->cursor_y) {
            rt->cursor_x = me.x;
            rt->cursor_y = me.y;
            frame->cursor_changed = true;
        }

        bool left_now = (me.buttons & 0x1u) != 0;
        bool left_changed = (me.changed & 0x1u) != 0;
        bool left_pressed = left_changed && left_now;
        bool left_released = left_changed && !left_now;

        if (left_pressed) {
            int32_t icon_idx = hit_test_icon(WINDOW_COUNT, rt->cursor_x, rt->cursor_y);
            if (icon_idx >= 0) {
                if (!rt->windows[icon_idx].visible) {
                    rt->windows[icon_idx].visible = true;
                    if (icon_idx == SHELL_WINDOW_IDX) {
                        sync_shell_window(rt);
                    }
                    frame->scene_changed = true;
                }
                focus_window(rt, icon_idx, &frame->scene_changed);
                rt->dragging_idx = -1;
                continue;
            }

            int32_t win_idx = hit_test_window(rt->windows, rt->z_order, WINDOW_COUNT, rt->cursor_x, rt->cursor_y);
            if (win_idx >= 0) {
                focus_window(rt, win_idx, &frame->scene_changed);

                if (win_idx == FILES_WINDOW_IDX) {
                    int32_t entry_idx = files_entry_at_point(rt, rt->cursor_x, rt->cursor_y);
                    if (entry_idx >= 0) {
                        if (desktop_files_select_at_point(rt, rt->cursor_x, rt->cursor_y)) {
                            frame->scene_changed = true;
                        }

                        uint64_t now = (uint64_t)sys_gettime();
                        if (entry_idx == rt->files_last_click_entry &&
                            (uint64_t)(now - rt->files_last_click_tick) <= FILES_DOUBLE_CLICK_TICKS) {
                            bool changed = false;
                            shell_action_t action = desktop_files_activate_selected(rt, &changed);
                            apply_shell_action(rt, frame, action);
                            if (changed) frame->scene_changed = true;
                            rt->files_last_click_entry = -1;
                        } else {
                            rt->files_last_click_entry = entry_idx;
                            rt->files_last_click_tick = now;
                        }
                        rt->dragging_idx = -1;
                        continue;
                    }
                }

                if (window_close_hit(&rt->windows[win_idx], rt->cursor_x, rt->cursor_y)) {
                    rt->windows[win_idx].visible = false;
                    if (rt->dragging_idx == win_idx) rt->dragging_idx = -1;
                    if (rt->active_idx == win_idx) rt->active_idx = top_visible_window(rt->windows, rt->z_order, WINDOW_COUNT);
                    frame->scene_changed = true;
                    continue;
                }

                if (window_title_hit(&rt->windows[win_idx], rt->cursor_x, rt->cursor_y)) {
                    rt->dragging_idx = win_idx;
                    rt->drag_off_x = rt->cursor_x - rt->windows[win_idx].win.x;
                    rt->drag_off_y = rt->cursor_y - rt->windows[win_idx].win.y;
                } else {
                    rt->dragging_idx = -1;
                }
            } else {
                if (rt->active_idx != -1) {
                    rt->active_idx = -1;
                    frame->scene_changed = true;
                }
                rt->dragging_idx = -1;
            }
        }

        if (left_released) {
            rt->dragging_idx = -1;
        }

        if (is_valid_window_idx(rt->dragging_idx) && left_now && rt->windows[rt->dragging_idx].visible) {
            int32_t nx = rt->cursor_x - rt->drag_off_x;
            int32_t ny = rt->cursor_y - rt->drag_off_y;
            if (nx != rt->windows[rt->dragging_idx].win.x || ny != rt->windows[rt->dragging_idx].win.y) {
                rt->windows[rt->dragging_idx].win.x = nx;
                rt->windows[rt->dragging_idx].win.y = ny;
                frame->scene_changed = true;
            }
        }
    }
}
