#ifndef USERLAND_PROGRAMS_DESKTOP_H
#define USERLAND_PROGRAMS_DESKTOP_H

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
#define ICON_BG_OFF     GUI_COLOR_RGB(27, 56, 84)
#define ICON_FG         GUI_COLOR_RGB(237, 244, 252)
#define WINDOW_TEXT_FG  GUI_COLOR_RGB(33, 44, 58)

#define DESKTOP_TOP_H  32
#define DESKTOP_SIDE_W 150

#define ICON_W   84
#define ICON_H   84
#define ICON_X   20
#define ICON_Y0  52
#define ICON_GAP 20

#define CURSOR_POINT_COUNT 36
#define WINDOW_COUNT 3
#define MAX_DIRTY_RECTS 48
#define SHELL_WINDOW_IDX 0
#define FILES_WINDOW_IDX 1
#define NETWORK_WINDOW_IDX 2
#define SHELL_INPUT_MAX 96
#define SHELL_LINE_MAX 96
#define SHELL_SCROLLBACK 64
#define SHELL_VIEW_MAX 14
#define SHELL_ARG_MAX 16
#define SHELL_PATH_MAX 128
#define SHELL_HISTORY_MAX 24
#define PANEL_LINE_MAX 14
#define FILES_ENTRY_MAX 96

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

typedef struct {
    gui_window_t win;
    uint32_t theme_idx;
    const char** lines;
    uint32_t line_count;
    int32_t selected_line;
    uint32_t selected_bg_color;
    uint32_t selected_fg_color;
    bool visible;
} desktop_window_t;

typedef struct {
    rect_t rects[MAX_DIRTY_RECTS];
    uint32_t count;
    bool full_redraw;
} dirty_set_t;

typedef struct {
    int32_t x;
    int32_t y;
    bool valid;
    uint32_t pixels[CURSOR_POINT_COUNT];
} cursor_save_t;

typedef struct {
    char cwd[SHELL_PATH_MAX];
    char prev_cwd[SHELL_PATH_MAX];
    int32_t ext_pid;
    int32_t ext_pty;
    bool ext_running;
    char input[SHELL_INPUT_MAX];
    uint32_t input_len;
    char scrollback[SHELL_SCROLLBACK][SHELL_LINE_MAX];
    uint32_t scroll_count;
    uint32_t scroll_head;
    char prompt[SHELL_LINE_MAX];
    const char* view[SHELL_VIEW_MAX];
    uint32_t view_count;
    char history[SHELL_HISTORY_MAX][SHELL_INPUT_MAX];
    uint32_t history_count;
    uint32_t history_next;
} desktop_shell_t;

typedef enum {
    SHELL_ACTION_NONE = 0,
    SHELL_ACTION_HIDE_WINDOW = 1u << 0,
    SHELL_ACTION_FORCE_FULL_REPAINT = 1u << 1
} shell_action_t;

typedef struct {
    desktop_window_t windows[WINDOW_COUNT];
    desktop_shell_t shell_state;
    char files_cwd[SHELL_PATH_MAX];
    char files_lines[PANEL_LINE_MAX][SHELL_LINE_MAX];
    const char* files_view[PANEL_LINE_MAX];
    int32_t files_line_to_entry[PANEL_LINE_MAX];
    char files_entries[FILES_ENTRY_MAX][SHELL_PATH_MAX];
    uint8_t files_entry_is_dir[FILES_ENTRY_MAX];
    uint8_t files_entry_is_parent[FILES_ENTRY_MAX];
    uint32_t files_entry_count;
    int32_t files_selected_entry;
    uint32_t files_scroll_offset;
    uint64_t files_last_click_tick;
    int32_t files_last_click_entry;
    uint32_t files_line_count;
    char files_status[SHELL_LINE_MAX];
    char net_lines[PANEL_LINE_MAX][SHELL_LINE_MAX];
    const char* net_view[PANEL_LINE_MAX];
    uint32_t net_line_count;
    int32_t net_last_ping_ms;
    int32_t net_last_ping_ok;
    uint8_t net_last_dns_ip[4];
    int32_t net_last_dns_ok;
    uint64_t last_net_probe_tick;
    uint64_t last_panel_refresh_tick;
    int32_t z_order[WINDOW_COUNT];
    int32_t active_idx;
    int32_t cursor_x;
    int32_t cursor_y;
    int32_t dragging_idx;
    int32_t drag_off_x;
    int32_t drag_off_y;
    cursor_save_t cursor_save;
    uint32_t theme_count;
} desktop_runtime_t;

typedef struct {
    desktop_window_t windows[WINDOW_COUNT];
    int32_t z_order[WINDOW_COUNT];
    int32_t active_idx;
} desktop_snapshot_t;

typedef struct {
    bool quit;
    bool scene_changed;
    bool cursor_changed;
    bool force_full_redraw;
} desktop_frame_state_t;

static inline int32_t desktop_font_w(void) {
    return (int32_t)gui_font_width();
}

static inline int32_t desktop_font_h(void) {
    return (int32_t)gui_font_height();
}

static inline int32_t desktop_line_advance(void) {
    return desktop_font_h() + 2;
}

int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi);
rect_t make_rect(int32_t x, int32_t y, int32_t w, int32_t h);
bool rect_valid(rect_t r);
rect_t rect_expand(rect_t r, int32_t pad_x, int32_t pad_y);
bool rect_contains(rect_t outer, rect_t inner);
bool point_in_rect(int32_t x, int32_t y, rect_t r);
bool rect_intersects(rect_t a, rect_t b);
rect_t rect_intersection(rect_t a, rect_t b);
rect_t rect_union(rect_t a, rect_t b);
rect_t clip_to_fb(const gui_fb_info_t* fb, rect_t r);
rect_t desktop_top_rect(const gui_fb_info_t* fb);
rect_t desktop_side_rect(const gui_fb_info_t* fb);
rect_t icon_rect(uint32_t index);
bool point_in_fb(const gui_fb_info_t* fb, int32_t x, int32_t y);

uint32_t desktop_theme_count(void);
void window_apply_theme(desktop_window_t* w);
uint32_t window_title_height(const gui_window_t* win);
rect_t window_rect(const gui_window_t* win);
rect_t close_button_rect(const gui_window_t* win);
rect_t window_body_rect(const desktop_window_t* w);
bool window_geometry_changed(const desktop_window_t* a, const desktop_window_t* b);
bool window_style_changed(const desktop_window_t* a, const desktop_window_t* b);
void dirty_init(dirty_set_t* d);
void dirty_add(dirty_set_t* d, const gui_fb_info_t* fb, rect_t r);
void dirty_add_all_window_text(dirty_set_t* d, const gui_fb_info_t* fb, const desktop_window_t* windows, uint32_t count);
uint32_t count_visible_windows(const desktop_window_t* windows, uint32_t count);
int32_t top_visible_window(const desktop_window_t* windows, const int32_t* z_order, uint32_t count);
bool bring_to_front(int32_t* z_order, uint32_t count, int32_t idx);
int32_t cycle_focus_window(const desktop_window_t* windows, const int32_t* z_order, uint32_t count, int32_t active_idx);
int32_t hit_test_icon(uint32_t count, int32_t x, int32_t y);
int32_t hit_test_window(const desktop_window_t* windows, const int32_t* z_order, uint32_t count, int32_t x, int32_t y);
bool window_title_hit(const desktop_window_t* w, int32_t x, int32_t y);
bool window_close_hit(const desktop_window_t* w, int32_t x, int32_t y);
bool clamp_window(desktop_window_t* w, const gui_fb_info_t* fb);

void shell_init(desktop_shell_t* shell);
void shell_sync_window(desktop_shell_t* shell, desktop_window_t* window);
shell_action_t shell_submit_input(desktop_shell_t* shell);
shell_action_t shell_run_line(desktop_shell_t* shell, const char* line);
bool shell_pump_external(desktop_shell_t* shell);
void shell_handle_backspace(desktop_shell_t* shell);
void shell_handle_char(desktop_shell_t* shell, char c);

rect_t expand_repaint_region(const gui_fb_info_t* fb, rect_t region);
void repaint_region(const gui_fb_info_t* fb,
                    const desktop_window_t* windows,
                    uint32_t window_count,
                    const int32_t* z_order,
                    int32_t active_idx,
                    rect_t region);

void draw_cursor(const gui_fb_info_t* fb, int32_t x, int32_t y);
void cursor_capture_under(const gui_fb_info_t* fb, cursor_save_t* save, int32_t x, int32_t y);
void cursor_restore_under(const gui_fb_info_t* fb, cursor_save_t* save);

void desktop_runtime_init(desktop_runtime_t* rt, const gui_fb_info_t* fb);
bool desktop_refresh_files_panel(desktop_runtime_t* rt);
bool desktop_refresh_network_panel(desktop_runtime_t* rt);
bool desktop_files_select_next(desktop_runtime_t* rt, int32_t delta);
bool desktop_files_select_at_point(desktop_runtime_t* rt, int32_t x, int32_t y);
shell_action_t desktop_files_activate_selected(desktop_runtime_t* rt, bool* out_changed);
bool desktop_files_delete_selected(desktop_runtime_t* rt);
bool desktop_refresh_dynamic_panels(desktop_runtime_t* rt);
void desktop_snapshot_save(desktop_snapshot_t* snap, const desktop_runtime_t* rt);
void desktop_poll_keyboard(desktop_runtime_t* rt, desktop_frame_state_t* frame);
void desktop_poll_mouse(desktop_runtime_t* rt, desktop_frame_state_t* frame);
void desktop_finalize_state(desktop_runtime_t* rt, const gui_fb_info_t* fb, desktop_frame_state_t* frame);
void desktop_render_frame(const gui_fb_info_t* fb,
                          desktop_runtime_t* rt,
                          const desktop_snapshot_t* prev,
                          const desktop_frame_state_t* frame);
uint32_t desktop_frame_sleep_ms(const desktop_frame_state_t* frame);

int desktop_main(void);

#endif
