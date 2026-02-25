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
#define SHELL_INPUT_MAX 96
#define SHELL_LINE_MAX 96
#define SHELL_SCROLLBACK 64
#define SHELL_VIEW_MAX 14
#define SHELL_ARG_MAX 16
#define SHELL_PATH_MAX 128

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
    int8_t dx;
    int8_t dy;
    uint8_t kind;
} cursor_point_t;

typedef struct {
    char cwd[SHELL_PATH_MAX];
    char prev_cwd[SHELL_PATH_MAX];
    char input[SHELL_INPUT_MAX];
    uint32_t input_len;
    char scrollback[SHELL_SCROLLBACK][SHELL_LINE_MAX];
    uint32_t scroll_count;
    uint32_t scroll_head;
    char prompt[SHELL_LINE_MAX];
    const char* view[SHELL_VIEW_MAX];
    uint32_t view_count;
} desktop_shell_t;

static rect_t window_rect(const gui_window_t* win);

static const theme_t themes[] = {
    { GUI_COLOR_RGB(45, 70, 102), GUI_COLOR_RGB(59, 100, 146), GUI_COLOR_RGB(224, 233, 244) },
    { GUI_COLOR_RGB(71, 52, 103), GUI_COLOR_RGB(106, 72, 146), GUI_COLOR_RGB(230, 224, 242) },
    { GUI_COLOR_RGB(66, 72, 42), GUI_COLOR_RGB(102, 114, 66), GUI_COLOR_RGB(234, 238, 221) },
    { GUI_COLOR_RGB(103, 58, 49), GUI_COLOR_RGB(143, 84, 71), GUI_COLOR_RGB(244, 230, 225) }
};

static const char* k_shell_boot_lines[] = {
    "SzczupakOS Shell (window mode)",
    "Type: help"
};

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

static const char* k_icon_labels[WINDOW_COUNT] = { "SHELL", "FILES", "NET" };

static const char* k_title = "SzczupakOS Desktop";
static const char* k_hint = "Tab focus  WASD move  C theme  X close  1-3 toggle  ESC quit";

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

static rect_t rect_expand(rect_t r, int32_t pad_x, int32_t pad_y) {
    r.x -= pad_x;
    r.y -= pad_y;
    r.w += pad_x * 2;
    r.h += pad_y * 2;
    return r;
}

static bool rect_contains(rect_t outer, rect_t inner) {
    if (!rect_valid(outer) || !rect_valid(inner)) return false;
    if (inner.x < outer.x) return false;
    if (inner.y < outer.y) return false;
    if (inner.x + inner.w > outer.x + outer.w) return false;
    if (inner.y + inner.h > outer.y + outer.h) return false;
    return true;
}

static bool point_in_rect(int32_t x, int32_t y, rect_t r) {
    if (!rect_valid(r)) return false;
    return (x >= r.x) && (y >= r.y) && (x < r.x + r.w) && (y < r.y + r.h);
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

static rect_t desktop_top_rect(const gui_fb_info_t* fb) {
    return make_rect(0, 0, (int32_t)fb->width, DESKTOP_TOP_H);
}

static rect_t desktop_side_rect(const gui_fb_info_t* fb) {
    int32_t h = (int32_t)fb->height - DESKTOP_TOP_H;
    if (h < 0) h = 0;
    return make_rect(0, DESKTOP_TOP_H, DESKTOP_SIDE_W, h);
}

static rect_t icon_rect(uint32_t index) {
    return make_rect(ICON_X, ICON_Y0 + (int32_t)index * (ICON_H + ICON_GAP), ICON_W, ICON_H);
}

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

    int32_t cx = x;
    int32_t cy = y;
    int32_t start_x = x;

    for (const char* p = text; *p; p++) {
        if (*p == '\n') {
            cx = start_x;
            cy += (int32_t)GUI_FONT_HEIGHT;
            continue;
        }

        rect_t cr = make_rect(cx, cy, (int32_t)GUI_FONT_WIDTH, (int32_t)GUI_FONT_HEIGHT);
        if (rect_contains(clip, cr)) {
            gui_draw_char((uint32_t)cx, (uint32_t)cy, *p, fg, bg);
        }
        cx += (int32_t)GUI_FONT_WIDTH;
    }
}

static rect_t expand_repaint_region(const gui_fb_info_t* fb, rect_t region) {
    rect_t out = rect_expand(region, (int32_t)GUI_FONT_WIDTH, (int32_t)GUI_FONT_HEIGHT);
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

static void window_apply_theme(desktop_window_t* w) {
    if (!w) return;

    uint32_t theme_count = (uint32_t)(sizeof(themes) / sizeof(themes[0]));
    w->theme_idx %= theme_count;
    w->win.border_color = themes[w->theme_idx].border;
    w->win.title_bg_color = themes[w->theme_idx].title_bg;
    w->win.body_color = themes[w->theme_idx].body_bg;
    w->win.title_fg_color = GUI_COLOR_RGB(245, 248, 253);
}

static uint32_t window_title_height(const gui_window_t* win) {
    uint32_t title_h = GUI_FONT_HEIGHT + 6;
    if (title_h + 2 > win->height) {
        title_h = (win->height > 2) ? (win->height - 2) : 0;
    }
    return title_h;
}

static rect_t window_rect(const gui_window_t* win) {
    return make_rect(win->x, win->y, (int32_t)win->width, (int32_t)win->height);
}

static rect_t close_button_rect(const gui_window_t* win) {
    uint32_t title_h = window_title_height(win);
    if (win->width < 38 || title_h < 12) return make_rect(0, 0, 0, 0);
    return make_rect(win->x + (int32_t)win->width - 16, win->y + 4, 10, 10);
}

static rect_t window_body_rect(const desktop_window_t* w) {
    if (!w) return make_rect(0, 0, 0, 0);
    int32_t title_h = (int32_t)window_title_height(&w->win);
    return make_rect(w->win.x + 1, w->win.y + 1 + title_h, (int32_t)w->win.width - 2, (int32_t)w->win.height - title_h - 2);
}

static bool window_geometry_changed(const desktop_window_t* a, const desktop_window_t* b) {
    if (!a || !b) return false;
    return a->win.x != b->win.x || a->win.y != b->win.y || a->win.width != b->win.width || a->win.height != b->win.height;
}

static bool window_style_changed(const desktop_window_t* a, const desktop_window_t* b) {
    if (!a || !b) return false;
    return a->theme_idx != b->theme_idx;
}

static void dirty_init(dirty_set_t* d) {
    if (!d) return;
    d->count = 0;
    d->full_redraw = false;
}

static void dirty_add(dirty_set_t* d, const gui_fb_info_t* fb, rect_t r) {
    if (!d || !fb || d->full_redraw) return;

    r = clip_to_fb(fb, r);
    if (!rect_valid(r)) return;

    for (uint32_t i = 0; i < d->count; i++) {
        if (rect_intersects(d->rects[i], r)) {
            d->rects[i] = rect_union(d->rects[i], r);
            return;
        }
    }

    if (d->count < MAX_DIRTY_RECTS) {
        d->rects[d->count++] = r;
        return;
    }

    d->full_redraw = true;
    d->count = 0;
}

static bool point_in_fb(const gui_fb_info_t* fb, int32_t x, int32_t y) {
    if (!fb) return false;
    if (x < 0 || y < 0) return false;
    if (x >= (int32_t)fb->width) return false;
    if (y >= (int32_t)fb->height) return false;
    return true;
}

typedef enum {
    SHELL_ACTION_NONE = 0,
    SHELL_ACTION_HIDE_WINDOW = 1
} shell_action_t;

static void shell_copy_cstr(char* dst, uint32_t cap, const char* src) {
    if (!dst || cap == 0) return;
    uint32_t i = 0;
    if (src) {
        while (src[i] && i + 1 < cap) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static void shell_buf_append_char(char* dst, uint32_t cap, uint32_t* pos, char c) {
    if (!dst || !pos || cap == 0) return;
    if (*pos + 1 >= cap) return;
    dst[*pos] = c;
    (*pos)++;
    dst[*pos] = '\0';
}

static void shell_buf_append_str(char* dst, uint32_t cap, uint32_t* pos, const char* src) {
    if (!dst || !pos || cap == 0 || !src) return;
    for (uint32_t i = 0; src[i]; i++) {
        if (*pos + 1 >= cap) break;
        dst[*pos] = src[i];
        (*pos)++;
    }
    dst[*pos] = '\0';
}

static void shell_push_scrollback_line(desktop_shell_t* shell, const char* line) {
    if (!shell) return;
    uint32_t idx = shell->scroll_head;
    shell_copy_cstr(shell->scrollback[idx], SHELL_LINE_MAX, line ? line : "");
    shell->scroll_head = (shell->scroll_head + 1) % SHELL_SCROLLBACK;
    if (shell->scroll_count < SHELL_SCROLLBACK) {
        shell->scroll_count++;
    }
}

static void shell_push_text(desktop_shell_t* shell, const char* text) {
    if (!shell || !text) return;

    char line[SHELL_LINE_MAX];
    uint32_t pos = 0;
    bool wrote_line = false;

    for (uint32_t i = 0; text[i]; i++) {
        char c = text[i];
        if (c == '\r') continue;
        if (c == '\n') {
            line[pos] = '\0';
            shell_push_scrollback_line(shell, line);
            pos = 0;
            wrote_line = true;
            continue;
        }
        if (c == '\t') c = ' ';
        if ((unsigned char)c < 32 || (unsigned char)c > 126) continue;

        if (pos + 1 >= SHELL_LINE_MAX) {
            line[pos] = '\0';
            shell_push_scrollback_line(shell, line);
            pos = 0;
            wrote_line = true;
        }
        line[pos++] = c;
    }

    if (pos > 0 || !wrote_line) {
        line[pos] = '\0';
        shell_push_scrollback_line(shell, line);
    }
}

static void shell_build_prompt(desktop_shell_t* shell) {
    if (!shell) return;
    uint32_t pos = 0;
    shell->prompt[0] = '\0';
    shell_buf_append_char(shell->prompt, SHELL_LINE_MAX, &pos, '[');
    shell_buf_append_str(shell->prompt, SHELL_LINE_MAX, &pos, shell->cwd);
    shell_buf_append_str(shell->prompt, SHELL_LINE_MAX, &pos, "]$ ");
    shell_buf_append_str(shell->prompt, SHELL_LINE_MAX, &pos, shell->input);
    shell_buf_append_char(shell->prompt, SHELL_LINE_MAX, &pos, '_');
}

static void shell_refresh_view(desktop_shell_t* shell) {
    if (!shell) return;

    shell->view_count = 0;
    uint32_t history_slots = SHELL_VIEW_MAX - 1;
    uint32_t take = shell->scroll_count;
    if (take > history_slots) take = history_slots;
    uint32_t skip = shell->scroll_count - take;
    uint32_t oldest = (shell->scroll_count < SHELL_SCROLLBACK) ? 0 : shell->scroll_head;

    for (uint32_t i = 0; i < take; i++) {
        uint32_t logical = skip + i;
        uint32_t idx = (oldest + logical) % SHELL_SCROLLBACK;
        shell->view[shell->view_count++] = shell->scrollback[idx];
    }

    shell_build_prompt(shell);
    shell->view[shell->view_count++] = shell->prompt;
}

static void shell_sync_window(desktop_shell_t* shell, desktop_window_t* window) {
    if (!shell || !window) return;
    shell_refresh_view(shell);
    window->lines = shell->view;
    window->line_count = shell->view_count;
}

static int shell_normalize_path(const desktop_shell_t* shell, const char* input, char* out, uint32_t out_size) {
    if (!shell || !input || !out || out_size < 2) return -1;

    char combined[SHELL_PATH_MAX * 2];
    if (input[0] == '/') {
        if ((uint32_t)strlen(input) >= (uint32_t)sizeof(combined)) return -1;
        strcpy(combined, input);
    } else {
        uint32_t cwd_len = (uint32_t)strlen(shell->cwd);
        uint32_t input_len = (uint32_t)strlen(input);
        uint32_t need_slash = (cwd_len > 0 && shell->cwd[cwd_len - 1] != '/') ? 1u : 0u;
        if (cwd_len + need_slash + input_len >= (uint32_t)sizeof(combined)) return -1;
        strcpy(combined, shell->cwd);
        if (need_slash) strcat(combined, "/");
        strcat(combined, input);
    }

    uint32_t out_len = 1;
    out[0] = '/';
    out[1] = '\0';

    uint32_t i = 0;
    while (combined[i] != '\0') {
        while (combined[i] == '/') i++;
        if (combined[i] == '\0') break;

        char segment[SHELL_PATH_MAX];
        uint32_t seg_len = 0;
        while (combined[i] != '\0' && combined[i] != '/') {
            if (seg_len + 1 >= (uint32_t)sizeof(segment)) return -1;
            segment[seg_len++] = combined[i++];
        }
        segment[seg_len] = '\0';

        if (seg_len == 1 && segment[0] == '.') continue;

        if (seg_len == 2 && segment[0] == '.' && segment[1] == '.') {
            if (out_len > 1) {
                uint32_t p = out_len;
                while (p > 1 && out[p - 1] == '/') p--;
                while (p > 1 && out[p - 1] != '/') p--;
                out_len = (p > 1) ? (p - 1) : 1;
                out[out_len] = '\0';
            }
            continue;
        }

        if (out_len > 1) {
            if (out_len + 1 >= out_size) return -1;
            out[out_len++] = '/';
        }
        if (out_len + seg_len >= out_size) return -1;
        for (uint32_t j = 0; j < seg_len; j++) {
            out[out_len++] = segment[j];
        }
        out[out_len] = '\0';
    }

    if (out_len == 0) {
        out[0] = '/';
        out[1] = '\0';
    }
    return 0;
}

static int shell_parse_args(char* line, char** argv, int max_args) {
    if (!line || !argv || max_args <= 0) return 0;
    int argc = 0;
    char* token = strtok(line, " \t");
    while (token && argc < max_args) {
        argv[argc++] = token;
        token = strtok(NULL, " \t");
    }
    return argc;
}

static void shell_push_prompt_command(desktop_shell_t* shell, const char* cmd) {
    if (!shell || !cmd) return;
    char line[SHELL_LINE_MAX];
    uint32_t pos = 0;
    line[0] = '\0';
    shell_buf_append_char(line, SHELL_LINE_MAX, &pos, '[');
    shell_buf_append_str(line, SHELL_LINE_MAX, &pos, shell->cwd);
    shell_buf_append_str(line, SHELL_LINE_MAX, &pos, "]$ ");
    shell_buf_append_str(line, SHELL_LINE_MAX, &pos, cmd);
    shell_push_scrollback_line(shell, line);
}

static shell_action_t shell_execute_command(desktop_shell_t* shell, char* cmdline) {
    if (!shell || !cmdline || !cmdline[0]) return SHELL_ACTION_NONE;

    char* argv[SHELL_ARG_MAX];
    int argc = shell_parse_args(cmdline, argv, SHELL_ARG_MAX);
    if (argc == 0) return SHELL_ACTION_NONE;

    if (strcmp(argv[0], "help") == 0) {
        shell_push_text(shell, "Built-ins: help clear pwd cd ls dir echo touch mkdir exit quit");
        shell_push_text(shell, "External ELF in this window is not supported yet.");
        return SHELL_ACTION_NONE;
    }

    if (strcmp(argv[0], "clear") == 0) {
        shell->scroll_count = 0;
        shell->scroll_head = 0;
        return SHELL_ACTION_NONE;
    }

    if (strcmp(argv[0], "pwd") == 0) {
        shell_push_text(shell, shell->cwd);
        return SHELL_ACTION_NONE;
    }

    if (strcmp(argv[0], "echo") == 0) {
        char line[SHELL_LINE_MAX];
        uint32_t pos = 0;
        line[0] = '\0';
        for (int i = 1; i < argc; i++) {
            if (i > 1) shell_buf_append_char(line, SHELL_LINE_MAX, &pos, ' ');
            shell_buf_append_str(line, SHELL_LINE_MAX, &pos, argv[i]);
        }
        shell_push_text(shell, line);
        return SHELL_ACTION_NONE;
    }

    if (strcmp(argv[0], "cd") == 0) {
        const char* target = "/";
        if (argc >= 2) {
            if (strcmp(argv[1], "-") == 0) target = shell->prev_cwd;
            else target = argv[1];
        }

        char normalized[SHELL_PATH_MAX];
        if (shell_normalize_path(shell, target, normalized, sizeof(normalized)) != 0) {
            shell_push_text(shell, "cd: path too long");
            return SHELL_ACTION_NONE;
        }

        char probe[2];
        if (sys_listdir(normalized, probe, sizeof(probe)) < 0) {
            shell_push_text(shell, "cd: no such directory");
            return SHELL_ACTION_NONE;
        }

        if (strcmp(shell->cwd, normalized) != 0) {
            shell_copy_cstr(shell->prev_cwd, SHELL_PATH_MAX, shell->cwd);
            shell_copy_cstr(shell->cwd, SHELL_PATH_MAX, normalized);
        }
        return SHELL_ACTION_NONE;
    }

    if (strcmp(argv[0], "ls") == 0 || strcmp(argv[0], "dir") == 0) {
        char out[2048];
        long n = sys_listdir(shell->cwd, out, sizeof(out) - 1);
        if (n < 0) {
            shell_push_text(shell, "ls: failed");
        } else if (n == 0) {
            shell_push_text(shell, "(empty)");
        } else {
            out[n] = '\0';
            shell_push_text(shell, out);
        }
        return SHELL_ACTION_NONE;
    }

    if (strcmp(argv[0], "touch") == 0) {
        if (argc < 2) {
            shell_push_text(shell, "Usage: touch <path>");
            return SHELL_ACTION_NONE;
        }
        char path[SHELL_PATH_MAX];
        if (shell_normalize_path(shell, argv[1], path, sizeof(path)) != 0) {
            shell_push_text(shell, "touch: path too long");
            return SHELL_ACTION_NONE;
        }
        if (sys_fs_touch(path) == 0) shell_push_text(shell, "touch: ok");
        else shell_push_text(shell, "touch: failed");
        return SHELL_ACTION_NONE;
    }

    if (strcmp(argv[0], "mkdir") == 0) {
        if (argc < 2) {
            shell_push_text(shell, "Usage: mkdir <path>");
            return SHELL_ACTION_NONE;
        }
        char path[SHELL_PATH_MAX];
        if (shell_normalize_path(shell, argv[1], path, sizeof(path)) != 0) {
            shell_push_text(shell, "mkdir: path too long");
            return SHELL_ACTION_NONE;
        }
        if (sys_fs_mkdir(path) == 0) shell_push_text(shell, "mkdir: ok");
        else shell_push_text(shell, "mkdir: failed");
        return SHELL_ACTION_NONE;
    }

    if (strcmp(argv[0], "desktop") == 0) {
        shell_push_text(shell, "desktop: already running");
        return SHELL_ACTION_NONE;
    }

    if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) {
        shell_push_text(shell, "Shell window hidden.");
        return SHELL_ACTION_HIDE_WINDOW;
    }

    shell_push_text(shell, "Unknown command");
    return SHELL_ACTION_NONE;
}

static void shell_init(desktop_shell_t* shell) {
    if (!shell) return;
    memset(shell, 0, sizeof(*shell));
    shell_copy_cstr(shell->cwd, SHELL_PATH_MAX, "/");
    shell_copy_cstr(shell->prev_cwd, SHELL_PATH_MAX, "/");
    for (uint32_t i = 0; i < (uint32_t)(sizeof(k_shell_boot_lines) / sizeof(k_shell_boot_lines[0])); i++) {
        shell_push_scrollback_line(shell, k_shell_boot_lines[i]);
    }
    shell_refresh_view(shell);
}

static shell_action_t shell_submit_input(desktop_shell_t* shell) {
    if (!shell) return SHELL_ACTION_NONE;

    if (shell->input_len == 0) {
        return SHELL_ACTION_NONE;
    }

    char cmd[SHELL_INPUT_MAX];
    shell_copy_cstr(cmd, sizeof(cmd), shell->input);
    shell_push_prompt_command(shell, cmd);
    shell->input[0] = '\0';
    shell->input_len = 0;
    return shell_execute_command(shell, cmd);
}

static void shell_handle_backspace(desktop_shell_t* shell) {
    if (!shell || shell->input_len == 0) return;
    shell->input_len--;
    shell->input[shell->input_len] = '\0';
}

static void shell_handle_char(desktop_shell_t* shell, char c) {
    if (!shell) return;
    if ((unsigned char)c < 32 || (unsigned char)c > 126) return;
    if (shell->input_len + 1 >= SHELL_INPUT_MAX) return;
    shell->input[shell->input_len++] = c;
    shell->input[shell->input_len] = '\0';
}

static void dirty_add_window_text(dirty_set_t* d, const gui_fb_info_t* fb, const desktop_window_t* w) {
    if (!d || !fb || !w || !w->visible) return;

    uint32_t title_h = window_title_height(&w->win);
    if (w->win.title) {
        rect_t title_rect = make_rect(w->win.x + 8,
                                      w->win.y + 4,
                                      (int32_t)strlen(w->win.title) * (int32_t)GUI_FONT_WIDTH,
                                      (int32_t)GUI_FONT_HEIGHT);
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
                                     (int32_t)strlen(line) * (int32_t)GUI_FONT_WIDTH,
                                     (int32_t)GUI_FONT_HEIGHT);
        dirty_add(d, fb, rect_intersection(line_rect, body));
        line_y += 18;
    }
}

static void dirty_add_all_window_text(dirty_set_t* d, const gui_fb_info_t* fb, const desktop_window_t* windows, uint32_t count) {
    if (!windows) return;
    for (uint32_t i = 0; i < count; i++) {
        dirty_add_window_text(d, fb, &windows[i]);
    }
}

static uint32_t count_visible_windows(const desktop_window_t* windows, uint32_t count) {
    uint32_t visible = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (windows[i].visible) visible++;
    }
    return visible;
}

static int32_t top_visible_window(const desktop_window_t* windows, const int32_t* z_order, uint32_t count) {
    for (uint32_t i = count; i > 0; i--) {
        int32_t idx = z_order[i - 1];
        if (idx >= 0 && (uint32_t)idx < count && windows[idx].visible) return idx;
    }
    return -1;
}

static bool bring_to_front(int32_t* z_order, uint32_t count, int32_t idx) {
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

static int32_t cycle_focus_window(const desktop_window_t* windows, const int32_t* z_order, uint32_t count, int32_t active_idx) {
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

static int32_t hit_test_icon(uint32_t count, int32_t x, int32_t y) {
    for (uint32_t i = 0; i < count; i++) {
        if (point_in_rect(x, y, icon_rect(i))) return (int32_t)i;
    }
    return -1;
}

static int32_t hit_test_window(const desktop_window_t* windows, const int32_t* z_order, uint32_t count, int32_t x, int32_t y) {
    for (uint32_t i = count; i > 0; i--) {
        int32_t idx = z_order[i - 1];
        if (idx < 0 || (uint32_t)idx >= count) continue;
        if (!windows[idx].visible) continue;
        if (point_in_rect(x, y, window_rect(&windows[idx].win))) return idx;
    }
    return -1;
}

static bool window_title_hit(const desktop_window_t* w, int32_t x, int32_t y) {
    if (!w || !w->visible) return false;
    uint32_t title_h = window_title_height(&w->win);
    rect_t title = make_rect(w->win.x, w->win.y, (int32_t)w->win.width, (int32_t)title_h + 1);
    return point_in_rect(x, y, title);
}

static bool window_close_hit(const desktop_window_t* w, int32_t x, int32_t y) {
    if (!w || !w->visible) return false;
    return point_in_rect(x, y, close_button_rect(&w->win));
}

static bool clamp_window(desktop_window_t* w, const gui_fb_info_t* fb) {
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
        line_y += 18;
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
    int32_t hint_w = (int32_t)strlen(k_hint) * (int32_t)GUI_FONT_WIDTH;
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

static void repaint_region(const gui_fb_info_t* fb,
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

static void draw_cursor(const gui_fb_info_t* fb, int32_t x, int32_t y) {
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

static void cursor_capture_under(const gui_fb_info_t* fb, cursor_save_t* save, int32_t x, int32_t y) {
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

static void cursor_restore_under(const gui_fb_info_t* fb, cursor_save_t* save) {
    if (!fb || !save || !save->valid) return;

    for (uint32_t i = 0; i < CURSOR_POINT_COUNT; i++) {
        int32_t px = save->x + (int32_t)k_cursor_points[i].dx;
        int32_t py = save->y + (int32_t)k_cursor_points[i].dy;
        if (!point_in_fb(fb, px, py)) continue;
        gui_putpixel((uint32_t)px, (uint32_t)py, save->pixels[i]);
    }

    save->valid = false;
}

int main(void) {
    gui_fb_info_t fb;
    if (gui_get_fb_info(&fb) < 0) {
        printf("desktop: framebuffer unavailable\n");
        return 1;
    }

    desktop_window_t windows[WINDOW_COUNT] = {
        {
            .win = {.x = 220, .y = 86, .width = 500, .height = 286, .title = "Shell.elf"},
            .theme_idx = 0,
            .lines = k_shell_boot_lines,
            .line_count = (uint32_t)(sizeof(k_shell_boot_lines) / sizeof(k_shell_boot_lines[0])),
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

    int32_t z_order[WINDOW_COUNT] = {1, 2, 0};
    int32_t active_idx = SHELL_WINDOW_IDX;

    for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
        window_apply_theme(&windows[i]);
        clamp_window(&windows[i], &fb);
    }
    shell_sync_window(&shell_state, &windows[SHELL_WINDOW_IDX]);

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
                if (next != active_idx) {
                    active_idx = next;
                    scene_changed = true;
                }
                if (next >= 0 && bring_to_front(z_order, WINDOW_COUNT, next)) {
                    scene_changed = true;
                }
                continue;
            }

            bool shell_focused = (active_idx == SHELL_WINDOW_IDX && windows[SHELL_WINDOW_IDX].visible);
            if (shell_focused) {
                if (c == '\b') {
                    uint32_t old_len = shell_state.input_len;
                    shell_handle_backspace(&shell_state);
                    if (shell_state.input_len != old_len) {
                        shell_sync_window(&shell_state, &windows[SHELL_WINDOW_IDX]);
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
                    shell_sync_window(&shell_state, &windows[SHELL_WINDOW_IDX]);
                    scene_changed = true;
                    continue;
                }

                uint32_t old_len = shell_state.input_len;
                shell_handle_char(&shell_state, c);
                if (shell_state.input_len != old_len) {
                    shell_sync_window(&shell_state, &windows[SHELL_WINDOW_IDX]);
                    scene_changed = true;
                }
                continue;
            }

            if (c == 'q' || c == 'Q') {
                quit = true;
                break;
            }

            if (c >= '1' && c <= '3') {
                int32_t idx = c - '1';
                if ((uint32_t)idx < WINDOW_COUNT) {
                    windows[idx].visible = !windows[idx].visible;
                    if (windows[idx].visible) {
                        if (idx == SHELL_WINDOW_IDX) {
                            shell_sync_window(&shell_state, &windows[SHELL_WINDOW_IDX]);
                        }
                        active_idx = idx;
                        if (bring_to_front(z_order, WINDOW_COUNT, idx)) {
                            scene_changed = true;
                        }
                    } else {
                        if (dragging_idx == idx) dragging_idx = -1;
                        if (active_idx == idx) active_idx = top_visible_window(windows, z_order, WINDOW_COUNT);
                    }
                    scene_changed = true;
                }
                continue;
            }

            if (active_idx < 0 || (uint32_t)active_idx >= WINDOW_COUNT) continue;
            if (!windows[active_idx].visible) continue;

            if (c == 'w' || c == 'W') {
                windows[active_idx].win.y -= 12;
                scene_changed = true;
            } else if (c == 's' || c == 'S') {
                windows[active_idx].win.y += 12;
                scene_changed = true;
            } else if (c == 'a' || c == 'A') {
                windows[active_idx].win.x -= 12;
                scene_changed = true;
            } else if (c == 'd' || c == 'D') {
                windows[active_idx].win.x += 12;
                scene_changed = true;
            } else if (c == 'c' || c == 'C') {
                uint32_t theme_count = (uint32_t)(sizeof(themes) / sizeof(themes[0]));
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
                            shell_sync_window(&shell_state, &windows[SHELL_WINDOW_IDX]);
                        }
                        scene_changed = true;
                    }
                    if (active_idx != icon_idx) {
                        active_idx = icon_idx;
                        scene_changed = true;
                    }
                    if (bring_to_front(z_order, WINDOW_COUNT, icon_idx)) {
                        scene_changed = true;
                    }
                    dragging_idx = -1;
                    continue;
                }

                int32_t win_idx = hit_test_window(windows, z_order, WINDOW_COUNT, cursor_x, cursor_y);
                if (win_idx >= 0) {
                    if (active_idx != win_idx) {
                        active_idx = win_idx;
                        scene_changed = true;
                    }
                    if (bring_to_front(z_order, WINDOW_COUNT, win_idx)) {
                        scene_changed = true;
                    }

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

            if (dragging_idx >= 0 && (uint32_t)dragging_idx < WINDOW_COUNT && left_now && windows[dragging_idx].visible) {
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

        if (active_idx >= 0 && ((uint32_t)active_idx >= WINDOW_COUNT || !windows[active_idx].visible)) {
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
            dirty_add_all_window_text(&dirty, &fb, windows, WINDOW_COUNT);
        }

        if (dragging_idx >= 0 && scene_changed) {
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
