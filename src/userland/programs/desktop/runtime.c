#include "desktop.h"

enum {
    PANEL_REFRESH_TICKS = 50,
    PANEL_LIST_BUF_SIZE = 2048,
    FILES_HEADER_LINES = 2,
    NET_PROBE_INTERVAL_TICKS = 500,
    NET_PING_TIMEOUT_MS = 140,
    NET_DNS_TIMEOUT_MS = 180
};

static void panel_copy_cstr(char* dst, uint32_t cap, const char* src) {
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

static void panel_buf_append_char(char* dst, uint32_t cap, uint32_t* pos, char c) {
    if (!dst || !pos || cap == 0) return;
    if (*pos + 1 >= cap) return;
    dst[*pos] = c;
    (*pos)++;
    dst[*pos] = '\0';
}

static void panel_buf_append_str(char* dst, uint32_t cap, uint32_t* pos, const char* src) {
    if (!dst || !pos || cap == 0 || !src) return;
    for (uint32_t i = 0; src[i]; i++) {
        if (*pos + 1 >= cap) break;
        dst[*pos] = src[i];
        (*pos)++;
    }
    dst[*pos] = '\0';
}

static void panel_buf_append_u64(char* dst, uint32_t cap, uint32_t* pos, uint64_t v) {
    if (!dst || !pos || cap == 0) return;
    if (v == 0) {
        panel_buf_append_char(dst, cap, pos, '0');
        return;
    }

    char tmp[24];
    uint32_t n = 0;
    while (v > 0 && n < (uint32_t)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0) {
        panel_buf_append_char(dst, cap, pos, tmp[--n]);
    }
}

static void panel_buf_append_hex_byte(char* dst, uint32_t cap, uint32_t* pos, uint8_t v) {
    static const char* k_hex = "0123456789ABCDEF";
    panel_buf_append_char(dst, cap, pos, k_hex[(v >> 4) & 0x0Fu]);
    panel_buf_append_char(dst, cap, pos, k_hex[v & 0x0Fu]);
}

static void panel_buf_append_ip(char* dst, uint32_t cap, uint32_t* pos, const uint8_t ip[4]) {
    for (uint32_t i = 0; i < 4; i++) {
        if (i > 0) panel_buf_append_char(dst, cap, pos, '.');
        panel_buf_append_u64(dst, cap, pos, ip[i]);
    }
}

static void panel_lines_reset(char lines[PANEL_LINE_MAX][SHELL_LINE_MAX], const char** view, uint32_t* count) {
    if (!lines || !view || !count) return;
    *count = 0;
    for (uint32_t i = 0; i < PANEL_LINE_MAX; i++) {
        lines[i][0] = '\0';
        view[i] = lines[i];
    }
}

static void panel_add_line(char lines[PANEL_LINE_MAX][SHELL_LINE_MAX], const char** view, uint32_t* count, const char* text) {
    if (!lines || !view || !count || *count >= PANEL_LINE_MAX) return;
    panel_copy_cstr(lines[*count], SHELL_LINE_MAX, text ? text : "");
    view[*count] = lines[*count];
    (*count)++;
}

static bool panel_is_printable(char c) {
    return (unsigned char)c >= 32u && (unsigned char)c <= 126u;
}

static char panel_upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    return c;
}

static bool files_path_has_elf_ext(const char* path) {
    if (!path) return false;
    uint32_t len = (uint32_t)strlen(path);
    if (len < 4u) return false;
    return panel_upper_ascii(path[len - 4u]) == '.' &&
           panel_upper_ascii(path[len - 3u]) == 'E' &&
           panel_upper_ascii(path[len - 2u]) == 'L' &&
           panel_upper_ascii(path[len - 1u]) == 'F';
}

static bool files_build_child_path(const char* cwd, const char* name, char* out, uint32_t out_size) {
    if (!cwd || !name || !out || out_size < 2) return false;

    char trimmed[SHELL_PATH_MAX];
    panel_copy_cstr(trimmed, sizeof(trimmed), name);
    uint32_t len = (uint32_t)strlen(trimmed);
    while (len > 0 && trimmed[len - 1] == '/') {
        trimmed[--len] = '\0';
    }
    if (len == 0) return false;

    if (strcmp(cwd, "/") == 0) {
        if (len + 2 >= out_size) return false;
        out[0] = '/';
        memcpy(out + 1, trimmed, len + 1);
        return true;
    }

    uint32_t cwd_len = (uint32_t)strlen(cwd);
    if (cwd_len + 1 + len >= out_size) return false;
    memcpy(out, cwd, cwd_len);
    out[cwd_len] = '/';
    memcpy(out + cwd_len + 1, trimmed, len + 1);
    return true;
}

static void files_parent_path(const char* cwd, char* out, uint32_t out_size) {
    if (!cwd || !out || out_size < 2) {
        return;
    }

    panel_copy_cstr(out, out_size, cwd);
    uint32_t len = (uint32_t)strlen(out);

    while (len > 1 && out[len - 1] == '/') {
        out[--len] = '\0';
    }

    while (len > 1 && out[len - 1] != '/') {
        out[--len] = '\0';
    }

    if (len > 1) {
        out[len - 1] = '\0';
    }

    if (out[0] == '\0') {
        out[0] = '/';
        out[1] = '\0';
    }
}

static bool files_apply_cwd(desktop_runtime_t* rt, const char* new_cwd) {
    if (!rt || !new_cwd || new_cwd[0] != '/') return false;
    if (strcmp(rt->shell_state.cwd, new_cwd) == 0) return false;

    panel_copy_cstr(rt->shell_state.prev_cwd, sizeof(rt->shell_state.prev_cwd), rt->shell_state.cwd);
    panel_copy_cstr(rt->shell_state.cwd, sizeof(rt->shell_state.cwd), new_cwd);
    return true;
}

static void files_entries_reset(desktop_runtime_t* rt) {
    if (!rt) return;
    rt->files_entry_count = 0;
    for (uint32_t i = 0; i < FILES_ENTRY_MAX; i++) {
        rt->files_entries[i][0] = '\0';
        rt->files_entry_is_dir[i] = 0;
        rt->files_entry_is_parent[i] = 0;
    }
}

static bool files_add_entry(desktop_runtime_t* rt, const char* name, bool is_dir, bool is_parent) {
    if (!rt || !name) return false;
    if (rt->files_entry_count >= FILES_ENTRY_MAX) return false;

    uint32_t idx = rt->files_entry_count++;
    panel_copy_cstr(rt->files_entries[idx], SHELL_PATH_MAX, name);
    rt->files_entry_is_dir[idx] = is_dir ? 1u : 0u;
    rt->files_entry_is_parent[idx] = is_parent ? 1u : 0u;
    return true;
}

static uint32_t files_visible_slots(void) {
    if (PANEL_LINE_MAX <= FILES_HEADER_LINES) return 0;
    return PANEL_LINE_MAX - FILES_HEADER_LINES;
}

static bool files_rebuild_view(desktop_runtime_t* rt) {
    if (!rt) return false;

    panel_lines_reset(rt->files_lines, rt->files_view, &rt->files_line_count);
    for (uint32_t i = 0; i < PANEL_LINE_MAX; i++) {
        rt->files_line_to_entry[i] = -1;
    }

    if (rt->files_entry_count == 0) {
        files_add_entry(rt, "..", true, true);
    }

    if (rt->files_selected_entry < 0 || (uint32_t)rt->files_selected_entry >= rt->files_entry_count) {
        rt->files_selected_entry = 0;
    }

    uint32_t slots = files_visible_slots();
    if (slots == 0) slots = 1;

    if (rt->files_entry_count <= slots) {
        rt->files_scroll_offset = 0;
    } else {
        if (rt->files_scroll_offset >= rt->files_entry_count) {
            rt->files_scroll_offset = rt->files_entry_count - 1;
        }
        if ((uint32_t)rt->files_selected_entry < rt->files_scroll_offset) {
            rt->files_scroll_offset = (uint32_t)rt->files_selected_entry;
        }
        if ((uint32_t)rt->files_selected_entry >= rt->files_scroll_offset + slots) {
            rt->files_scroll_offset = (uint32_t)rt->files_selected_entry - slots + 1u;
        }
    }

    char line[SHELL_LINE_MAX];
    uint32_t pos = 0;

    line[0] = '\0';
    pos = 0;
    panel_buf_append_str(line, sizeof(line), &pos, "Path: ");
    panel_buf_append_str(line, sizeof(line), &pos, rt->shell_state.cwd);
    panel_add_line(rt->files_lines, rt->files_view, &rt->files_line_count, line);

    panel_add_line(rt->files_lines, rt->files_view, &rt->files_line_count,
                   (rt->files_status[0] != '\0') ? rt->files_status : "J/K select  O/Enter open  U up  D delete");

    uint32_t start = rt->files_scroll_offset;
    uint32_t shown = 0;
    for (uint32_t idx = start; idx < rt->files_entry_count && shown < slots; idx++, shown++) {
        line[0] = '\0';
        pos = 0;
        panel_buf_append_str(line, sizeof(line), &pos, ((int32_t)idx == rt->files_selected_entry) ? "> " : "  ");

        if (rt->files_entry_is_parent[idx]) {
            panel_buf_append_str(line, sizeof(line), &pos, "../");
        } else {
            panel_buf_append_str(line, sizeof(line), &pos, rt->files_entries[idx]);
            if (rt->files_entry_is_dir[idx]) {
                panel_buf_append_char(line, sizeof(line), &pos, '/');
            }
        }

        panel_add_line(rt->files_lines, rt->files_view, &rt->files_line_count, line);
        rt->files_line_to_entry[rt->files_line_count - 1u] = (int32_t)idx;
    }

    if (shown == 0) {
        panel_add_line(rt->files_lines, rt->files_view, &rt->files_line_count, "  (empty)");
    }

    rt->windows[FILES_WINDOW_IDX].lines = rt->files_view;
    rt->windows[FILES_WINDOW_IDX].line_count = rt->files_line_count;
    rt->windows[FILES_WINDOW_IDX].selected_line = -1;
    for (uint32_t i = FILES_HEADER_LINES; i < rt->files_line_count; i++) {
        if (rt->files_line_to_entry[i] == rt->files_selected_entry) {
            rt->windows[FILES_WINDOW_IDX].selected_line = (int32_t)i;
            break;
        }
    }
    return true;
}

static void init_z_order(int32_t* z_order) {
    if (!z_order) return;
    for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
        z_order[i] = (int32_t)i;
    }
    (void)bring_to_front(z_order, WINDOW_COUNT, SHELL_WINDOW_IDX);
}

static void init_window(desktop_window_t* w,
                        int32_t x,
                        int32_t y,
                        uint32_t width,
                        uint32_t height,
                        const char* title,
                        uint32_t theme_idx,
                        const char** lines,
                        uint32_t line_count) {
    if (!w) return;

    w->win.x = x;
    w->win.y = y;
    w->win.width = width;
    w->win.height = height;
    w->win.title = title;
    w->theme_idx = theme_idx;
    w->lines = lines;
    w->line_count = line_count;
    w->selected_line = -1;
    w->visible = true;
}

bool desktop_refresh_files_panel(desktop_runtime_t* rt) {
    if (!rt) return false;

    files_entries_reset(rt);
    (void)files_add_entry(rt, "..", true, true);

    char listing[PANEL_LIST_BUF_SIZE];
    long n = sys_listdir(rt->shell_state.cwd, listing, sizeof(listing) - 1);
    if (n < 0) {
        panel_copy_cstr(rt->files_status, sizeof(rt->files_status), "listdir failed");
    } else {
        listing[n] = '\0';
        const char* p = listing;
        while (*p) {
            while (*p == '\r' || *p == '\n') p++;
            if (*p == '\0') break;

            char entry[SHELL_PATH_MAX];
            uint32_t pos = 0;
            while (*p && *p != '\r' && *p != '\n') {
                if (panel_is_printable(*p) && pos + 1 < (uint32_t)sizeof(entry)) {
                    entry[pos++] = *p;
                }
                p++;
            }
            entry[pos] = '\0';

            bool is_dir = false;
            while (pos > 0 && entry[pos - 1] == '/') {
                entry[--pos] = '\0';
                is_dir = true;
            }

            if (entry[0] != '\0' && strcmp(entry, "(empty)") != 0) {
                (void)files_add_entry(rt, entry, is_dir, false);
            }

            while (*p == '\r' || *p == '\n') p++;
        }
    }

    panel_copy_cstr(rt->files_cwd, sizeof(rt->files_cwd), rt->shell_state.cwd);
    return files_rebuild_view(rt);
}

bool desktop_files_select_next(desktop_runtime_t* rt, int32_t delta) {
    if (!rt || delta == 0 || rt->files_entry_count == 0) return false;

    int32_t next = rt->files_selected_entry + delta;
    if (next < 0) next = 0;
    if ((uint32_t)next >= rt->files_entry_count) next = (int32_t)rt->files_entry_count - 1;
    if (next == rt->files_selected_entry) return false;

    rt->files_selected_entry = next;
    return files_rebuild_view(rt);
}

bool desktop_files_select_at_point(desktop_runtime_t* rt, int32_t x, int32_t y) {
    if (!rt) return false;

    const desktop_window_t* w = &rt->windows[FILES_WINDOW_IDX];
    if (!w->visible) return false;

    rect_t body = window_body_rect(w);
    if (!point_in_rect(x, y, body)) return false;

    int32_t line_y0 = w->win.y + 1 + (int32_t)window_title_height(&w->win) + 10;
    int32_t rel = y - line_y0;
    if (rel < 0) return false;

    int32_t line_idx = rel / desktop_line_advance();
    if (line_idx < 0 || (uint32_t)line_idx >= w->line_count) return false;

    int32_t entry_idx = rt->files_line_to_entry[line_idx];
    if (entry_idx < 0 || (uint32_t)entry_idx >= rt->files_entry_count) return false;

    if (entry_idx == rt->files_selected_entry) return false;
    rt->files_selected_entry = entry_idx;
    return files_rebuild_view(rt);
}

shell_action_t desktop_files_activate_selected(desktop_runtime_t* rt, bool* out_changed) {
    if (out_changed) *out_changed = false;
    if (!rt || rt->files_entry_count == 0 || rt->files_selected_entry < 0 ||
        (uint32_t)rt->files_selected_entry >= rt->files_entry_count) {
        return SHELL_ACTION_NONE;
    }

    uint32_t idx = (uint32_t)rt->files_selected_entry;
    bool cwd_changed = false;

    if (rt->files_entry_is_parent[idx]) {
        char parent[SHELL_PATH_MAX];
        files_parent_path(rt->shell_state.cwd, parent, sizeof(parent));
        cwd_changed = files_apply_cwd(rt, parent);
        panel_copy_cstr(rt->files_status, sizeof(rt->files_status), "opened parent directory");
    } else if (rt->files_entry_is_dir[idx]) {
        char path[SHELL_PATH_MAX];
        if (files_build_child_path(rt->shell_state.cwd, rt->files_entries[idx], path, sizeof(path))) {
            cwd_changed = files_apply_cwd(rt, path);
            panel_copy_cstr(rt->files_status, sizeof(rt->files_status), "opened directory");
        }
    } else {
        char path[SHELL_PATH_MAX];
        if (!files_build_child_path(rt->shell_state.cwd, rt->files_entries[idx], path, sizeof(path))) {
            panel_copy_cstr(rt->files_status, sizeof(rt->files_status), "path too long");
            if (out_changed) *out_changed = files_rebuild_view(rt);
            return SHELL_ACTION_NONE;
        }

        if (!files_path_has_elf_ext(path)) {
            panel_copy_cstr(rt->files_status, sizeof(rt->files_status), "open: only .ELF is executable");
            if (out_changed) *out_changed = files_rebuild_view(rt);
            return SHELL_ACTION_NONE;
        }

        shell_action_t action = shell_run_line(&rt->shell_state, path);
        shell_sync_window(&rt->shell_state, &rt->windows[SHELL_WINDOW_IDX]);
        panel_copy_cstr(rt->files_status, sizeof(rt->files_status), "executed ELF in shell window");
        if (out_changed) {
            *out_changed = true;
        }
        (void)files_rebuild_view(rt);
        return action;
    }

    if (cwd_changed) {
        shell_sync_window(&rt->shell_state, &rt->windows[SHELL_WINDOW_IDX]);
    }
    bool changed = desktop_refresh_files_panel(rt);
    if (out_changed) {
        *out_changed = changed || cwd_changed;
    }
    return SHELL_ACTION_NONE;
}

bool desktop_files_delete_selected(desktop_runtime_t* rt) {
    if (!rt || rt->files_entry_count == 0 || rt->files_selected_entry < 0 ||
        (uint32_t)rt->files_selected_entry >= rt->files_entry_count) {
        return false;
    }

    uint32_t idx = (uint32_t)rt->files_selected_entry;
    if (rt->files_entry_is_parent[idx]) {
        panel_copy_cstr(rt->files_status, sizeof(rt->files_status), "delete: parent entry is virtual");
        return files_rebuild_view(rt);
    }

    char path[SHELL_PATH_MAX];
    if (!files_build_child_path(rt->shell_state.cwd, rt->files_entries[idx], path, sizeof(path))) {
        panel_copy_cstr(rt->files_status, sizeof(rt->files_status), "delete: path too long");
        return files_rebuild_view(rt);
    }

    if (sys_fs_delete(path) == 0) {
        panel_copy_cstr(rt->files_status, sizeof(rt->files_status), "delete: ok");
    } else {
        panel_copy_cstr(rt->files_status, sizeof(rt->files_status), "delete: failed (dir not empty?)");
    }

    return desktop_refresh_files_panel(rt);
}

bool desktop_refresh_network_panel(desktop_runtime_t* rt) {
    if (!rt) return false;

    panel_lines_reset(rt->net_lines, rt->net_view, &rt->net_line_count);

    struct net_info info;
    if (sys_net_info(&info) < 0) {
        panel_add_line(rt->net_lines, rt->net_view, &rt->net_line_count, "net: unavailable");
        rt->net_last_ping_ok = -1;
        rt->net_last_dns_ok = -1;
    } else {
        panel_add_line(rt->net_lines, rt->net_view, &rt->net_line_count,
                       info.link_up ? "link: up" : "link: down");
        panel_add_line(rt->net_lines, rt->net_view, &rt->net_line_count,
                       info.configured ? "config: ready" : "config: pending");

        uint64_t now = (uint64_t)sys_gettime();
        bool can_probe = info.configured && (info.gateway[0] | info.gateway[1] | info.gateway[2] | info.gateway[3]);
        if (can_probe &&
            (rt->last_net_probe_tick == 0 || (uint64_t)(now - rt->last_net_probe_tick) >= NET_PROBE_INTERVAL_TICKS)) {
            uint32_t rtt = 0;
            rt->net_last_ping_ok = (sys_net_ping(info.gateway, NET_PING_TIMEOUT_MS, &rtt) == 0) ? 1 : 0;
            rt->net_last_ping_ms = (rt->net_last_ping_ok > 0) ? (int32_t)rtt : -1;

            uint8_t dns_ip[4] = {0, 0, 0, 0};
            rt->net_last_dns_ok = (sys_net_resolve("example.com", NET_DNS_TIMEOUT_MS, dns_ip) == 0) ? 1 : 0;
            if (rt->net_last_dns_ok > 0) {
                memcpy(rt->net_last_dns_ip, dns_ip, sizeof(rt->net_last_dns_ip));
            }
            rt->last_net_probe_tick = now;
        }

        char line[SHELL_LINE_MAX];
        uint32_t pos = 0;

        line[0] = '\0';
        pos = 0;
        panel_buf_append_str(line, sizeof(line), &pos, "mac: ");
        for (uint32_t i = 0; i < 6; i++) {
            if (i > 0) panel_buf_append_char(line, sizeof(line), &pos, ':');
            panel_buf_append_hex_byte(line, sizeof(line), &pos, info.mac[i]);
        }
        panel_add_line(rt->net_lines, rt->net_view, &rt->net_line_count, line);

        line[0] = '\0';
        pos = 0;
        panel_buf_append_str(line, sizeof(line), &pos, "ip: ");
        panel_buf_append_ip(line, sizeof(line), &pos, info.ip);
        panel_add_line(rt->net_lines, rt->net_view, &rt->net_line_count, line);

        line[0] = '\0';
        pos = 0;
        panel_buf_append_str(line, sizeof(line), &pos, "gw: ");
        panel_buf_append_ip(line, sizeof(line), &pos, info.gateway);
        panel_add_line(rt->net_lines, rt->net_view, &rt->net_line_count, line);

        line[0] = '\0';
        pos = 0;
        panel_buf_append_str(line, sizeof(line), &pos, "dns: ");
        panel_buf_append_ip(line, sizeof(line), &pos, info.dns);
        panel_add_line(rt->net_lines, rt->net_view, &rt->net_line_count, line);

        line[0] = '\0';
        pos = 0;
        panel_buf_append_str(line, sizeof(line), &pos, "gw ping: ");
        if (rt->net_last_ping_ok > 0) {
            panel_buf_append_u64(line, sizeof(line), &pos, (uint64_t)rt->net_last_ping_ms);
            panel_buf_append_str(line, sizeof(line), &pos, " ms");
        } else if (rt->net_last_ping_ok == 0) {
            panel_buf_append_str(line, sizeof(line), &pos, "timeout");
        } else {
            panel_buf_append_str(line, sizeof(line), &pos, "n/a");
        }
        panel_add_line(rt->net_lines, rt->net_view, &rt->net_line_count, line);

        line[0] = '\0';
        pos = 0;
        panel_buf_append_str(line, sizeof(line), &pos, "dns test: ");
        if (rt->net_last_dns_ok > 0) {
            panel_buf_append_str(line, sizeof(line), &pos, "example.com -> ");
            panel_buf_append_ip(line, sizeof(line), &pos, rt->net_last_dns_ip);
        } else if (rt->net_last_dns_ok == 0) {
            panel_buf_append_str(line, sizeof(line), &pos, "lookup failed");
        } else {
            panel_buf_append_str(line, sizeof(line), &pos, "n/a");
        }
        panel_add_line(rt->net_lines, rt->net_view, &rt->net_line_count, line);
    }

    struct net_stats stats;
    if (sys_net_stats(&stats) == 0) {
        char line[SHELL_LINE_MAX];
        uint32_t pos = 0;

        line[0] = '\0';
        pos = 0;
        panel_buf_append_str(line, sizeof(line), &pos, "rx:");
        panel_buf_append_u64(line, sizeof(line), &pos, stats.rx_frames);
        panel_buf_append_str(line, sizeof(line), &pos, " tx:");
        panel_buf_append_u64(line, sizeof(line), &pos, stats.tx_frames);
        panel_add_line(rt->net_lines, rt->net_view, &rt->net_line_count, line);

        line[0] = '\0';
        pos = 0;
        panel_buf_append_str(line, sizeof(line), &pos, "ipv4:");
        panel_buf_append_u64(line, sizeof(line), &pos, stats.rx_ipv4);
        panel_buf_append_str(line, sizeof(line), &pos, " arp:");
        panel_buf_append_u64(line, sizeof(line), &pos, stats.rx_arp);
        panel_add_line(rt->net_lines, rt->net_view, &rt->net_line_count, line);

        line[0] = '\0';
        pos = 0;
        panel_buf_append_str(line, sizeof(line), &pos, "icmp:");
        panel_buf_append_u64(line, sizeof(line), &pos, stats.rx_icmp);
        panel_buf_append_str(line, sizeof(line), &pos, " udp:");
        panel_buf_append_u64(line, sizeof(line), &pos, stats.rx_udp);
        panel_add_line(rt->net_lines, rt->net_view, &rt->net_line_count, line);

        line[0] = '\0';
        pos = 0;
        panel_buf_append_str(line, sizeof(line), &pos, "tcp:");
        panel_buf_append_u64(line, sizeof(line), &pos, stats.rx_tcp);
        panel_buf_append_str(line, sizeof(line), &pos, " dnsq:");
        panel_buf_append_u64(line, sizeof(line), &pos, stats.dns_queries);
        panel_add_line(rt->net_lines, rt->net_view, &rt->net_line_count, line);
    }

    rt->windows[NETWORK_WINDOW_IDX].lines = rt->net_view;
    rt->windows[NETWORK_WINDOW_IDX].line_count = rt->net_line_count;
    rt->windows[NETWORK_WINDOW_IDX].selected_line = -1;
    return true;
}

bool desktop_refresh_dynamic_panels(desktop_runtime_t* rt) {
    if (!rt) return false;

    bool changed = false;
    uint64_t now = (uint64_t)sys_gettime();
    bool periodic = (uint64_t)(now - rt->last_panel_refresh_tick) >= PANEL_REFRESH_TICKS;

    if (rt->windows[FILES_WINDOW_IDX].visible &&
        (strcmp(rt->files_cwd, rt->shell_state.cwd) != 0 || periodic)) {
        changed = desktop_refresh_files_panel(rt) || changed;
    }

    if (rt->windows[NETWORK_WINDOW_IDX].visible && periodic) {
        changed = desktop_refresh_network_panel(rt) || changed;
    }

    if (periodic) {
        rt->last_panel_refresh_tick = now;
    }

    return changed;
}

void desktop_runtime_init(desktop_runtime_t* rt, const gui_fb_info_t* fb) {
    if (!rt || !fb) return;

    memset(rt, 0, sizeof(*rt));
    init_window(&rt->windows[SHELL_WINDOW_IDX], 220, 86, 500, 286, "Shell.elf", 0, NULL, 0);
    init_window(&rt->windows[FILES_WINDOW_IDX], 360, 170, 390, 230, "Files", 1, NULL, 0);
    init_window(&rt->windows[NETWORK_WINDOW_IDX], 470, 116, 400, 220, "Network", 2, NULL, 0);

    shell_init(&rt->shell_state);
    panel_copy_cstr(rt->files_status, sizeof(rt->files_status), "J/K select  O/Enter open  U up  D delete");
    rt->files_selected_entry = 0;
    rt->files_last_click_entry = -1;
    rt->net_last_ping_ms = -1;
    rt->net_last_ping_ok = -1;
    rt->net_last_dns_ok = -1;

    rt->theme_count = desktop_theme_count();
    if (rt->theme_count == 0) rt->theme_count = 1;

    init_z_order(rt->z_order);
    rt->active_idx = SHELL_WINDOW_IDX;

    for (uint32_t i = 0; i < WINDOW_COUNT; i++) {
        window_apply_theme(&rt->windows[i]);
        clamp_window(&rt->windows[i], fb);
    }
    shell_sync_window(&rt->shell_state, &rt->windows[SHELL_WINDOW_IDX]);

    desktop_refresh_files_panel(rt);
    desktop_refresh_network_panel(rt);
    rt->last_panel_refresh_tick = (uint64_t)sys_gettime();

    rt->cursor_x = (int32_t)(fb->width / 2);
    rt->cursor_y = (int32_t)(fb->height / 2);
    rt->dragging_idx = -1;
    rt->drag_off_x = 0;
    rt->drag_off_y = 0;
}

void desktop_snapshot_save(desktop_snapshot_t* snap, const desktop_runtime_t* rt) {
    if (!snap || !rt) return;
    memcpy(snap->windows, rt->windows, sizeof(snap->windows));
    memcpy(snap->z_order, rt->z_order, sizeof(snap->z_order));
    snap->active_idx = rt->active_idx;
}
