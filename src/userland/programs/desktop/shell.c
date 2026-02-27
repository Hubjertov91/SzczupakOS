#include "desktop.h"

static const char* k_shell_boot_lines[] = {
    "SzczupakOS Shell (window mode)",
    "Type: help"
};

#define SHELL_EXEC_CMD_MAX 512

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

static void shell_buf_append_u64(char* dst, uint32_t cap, uint32_t* pos, uint64_t v) {
    if (!dst || !pos || cap == 0) return;
    if (v == 0) {
        shell_buf_append_char(dst, cap, pos, '0');
        return;
    }

    char tmp[24];
    uint32_t n = 0;
    while (v > 0 && n < (uint32_t)sizeof(tmp)) {
        tmp[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (n > 0) {
        shell_buf_append_char(dst, cap, pos, tmp[--n]);
    }
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
    if (shell->ext_running) {
        shell_buf_append_str(shell->prompt, SHELL_LINE_MAX, &pos, "(pty)$ ");
    } else {
        shell_buf_append_char(shell->prompt, SHELL_LINE_MAX, &pos, '[');
        shell_buf_append_str(shell->prompt, SHELL_LINE_MAX, &pos, shell->cwd);
        shell_buf_append_str(shell->prompt, SHELL_LINE_MAX, &pos, "]$ ");
    }
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

void shell_sync_window(desktop_shell_t* shell, desktop_window_t* window) {
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

static char shell_upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    return c;
}

static void shell_to_upper_path(const char* in, char* out, uint32_t out_size) {
    if (!in || !out || out_size == 0) return;
    uint32_t i = 0;
    while (in[i] != '\0' && i + 1 < out_size) {
        out[i] = shell_upper_ascii(in[i]);
        i++;
    }
    out[i] = '\0';
}

static bool shell_basename_has_dot(const char* path) {
    if (!path) return false;
    const char* base = path;
    for (const char* p = path; *p; p++) {
        if (*p == '/') base = p + 1;
    }
    for (const char* p = base; *p; p++) {
        if (*p == '.') return true;
    }
    return false;
}

static int shell_append_elf_ext(const char* in, char* out, uint32_t out_size) {
    if (!in || !out || out_size == 0) return -1;
    uint32_t len = (uint32_t)strlen(in);
    if (len + 4 >= out_size) return -1;
    strcpy(out, in);
    strcat(out, ".ELF");
    return 0;
}

static int shell_build_exec_cmdline(const char* exec_path, int arg_count, char** args, char* out, uint32_t out_size) {
    if (!exec_path || !out || out_size == 0) return -1;

    uint32_t pos = 0;
    uint32_t path_len = (uint32_t)strlen(exec_path);
    if (path_len == 0 || path_len >= out_size) return -1;
    memcpy(out, exec_path, path_len);
    pos += path_len;

    for (int i = 0; i < arg_count; i++) {
        const char* arg = args[i];
        if (!arg || arg[0] == '\0') continue;

        uint32_t arg_len = (uint32_t)strlen(arg);
        if (pos + 1 + arg_len >= out_size) return -1;
        out[pos++] = ' ';
        memcpy(out + pos, arg, arg_len);
        pos += arg_len;
    }

    out[pos] = '\0';
    return 0;
}

static long shell_spawn_path_with_args_pty(const char* exec_path, int arg_count, char** args, int32_t pty_id) {
    char exec_cmdline[SHELL_EXEC_CMD_MAX];
    if (shell_build_exec_cmdline(exec_path, arg_count, args, exec_cmdline, sizeof(exec_cmdline)) != 0) {
        return -1;
    }
    return sys_pty_spawn(exec_cmdline, pty_id);
}

static long shell_try_spawn_command_pty(const desktop_shell_t* shell, const char* cmd, int arg_count, char** args, int32_t pty_id) {
    if (!shell || !cmd || cmd[0] == '\0') return -1;

    char abs_path[SHELL_PATH_MAX];
    if (shell_normalize_path(shell, cmd, abs_path, sizeof(abs_path)) != 0) return -1;

    long pid = shell_spawn_path_with_args_pty(abs_path, arg_count, args, pty_id);
    if (pid >= 0) return pid;

    char upper_path[SHELL_PATH_MAX];
    shell_to_upper_path(abs_path, upper_path, sizeof(upper_path));
    if (strcmp(upper_path, abs_path) != 0) {
        pid = shell_spawn_path_with_args_pty(upper_path, arg_count, args, pty_id);
        if (pid >= 0) return pid;
    }

    if (!shell_basename_has_dot(abs_path)) {
        char abs_elf[SHELL_PATH_MAX];
        if (shell_append_elf_ext(abs_path, abs_elf, sizeof(abs_elf)) == 0) {
            pid = shell_spawn_path_with_args_pty(abs_elf, arg_count, args, pty_id);
            if (pid >= 0) return pid;

            char upper_elf[SHELL_PATH_MAX];
            shell_to_upper_path(abs_elf, upper_elf, sizeof(upper_elf));
            if (strcmp(upper_elf, abs_elf) != 0) {
                pid = shell_spawn_path_with_args_pty(upper_elf, arg_count, args, pty_id);
                if (pid >= 0) return pid;
            }
        }
    }

    return -1;
}

static bool shell_drain_pty_output(desktop_shell_t* shell, int32_t pty_id) {
    if (!shell || pty_id < 0) return false;

    bool changed = false;
    char out[257];
    while (1) {
        long avail = sys_pty_out_avail(pty_id);
        if (avail <= 0) break;

        while (avail > 0) {
            uint32_t chunk = (avail > 256) ? 256u : (uint32_t)avail;
            long n = sys_pty_read(pty_id, out, chunk);
            if (n <= 0) break;

            for (long i = 0; i < n; i++) {
                if (out[i] == '\0') out[i] = ' ';
                if (out[i] == '\f') out[i] = '\n';
            }
            out[n] = '\0';
            shell_push_text(shell, out);
            changed = true;
            avail -= n;
        }
    }

    return changed;
}

static void shell_clear_external_state(desktop_shell_t* shell) {
    if (!shell) return;
    shell->ext_running = false;
    shell->ext_pid = -1;
    shell->ext_pty = -1;
}

static void shell_push_exit_code(desktop_shell_t* shell, int32_t exit_code) {
    if (!shell || exit_code == 0) return;

    char line[SHELL_LINE_MAX];
    uint32_t pos = 0;
    line[0] = '\0';
    shell_buf_append_str(line, sizeof(line), &pos, "exit code=");
    if (exit_code < 0) {
        shell_buf_append_char(line, sizeof(line), &pos, '-');
        shell_buf_append_u64(line, sizeof(line), &pos, (uint64_t)(-(int64_t)exit_code));
    } else {
        shell_buf_append_u64(line, sizeof(line), &pos, (uint64_t)exit_code);
    }
    shell_push_text(shell, line);
}

static int shell_start_external_command(desktop_shell_t* shell, const char* cmd, int arg_count, char** args) {
    if (!shell || !cmd || cmd[0] == '\0') return 0;
    if (shell->ext_running) {
        shell_push_text(shell, "run: process already running");
        return -1;
    }

    int32_t pty_id = (int32_t)sys_pty_open();
    if (pty_id < 0) {
        shell_push_text(shell, "run: PTY unavailable");
        return -1;
    }

    long pid = shell_try_spawn_command_pty(shell, cmd, arg_count, args, pty_id);
    if (pid < 0) {
        (void)sys_pty_close(pty_id);
        return 0;
    }

    shell->ext_running = true;
    shell->ext_pid = (int32_t)pid;
    shell->ext_pty = pty_id;
    (void)shell_drain_pty_output(shell, pty_id);
    return 1;
}

bool shell_pump_external(desktop_shell_t* shell) {
    if (!shell || !shell->ext_running) return false;

    bool changed = shell_drain_pty_output(shell, shell->ext_pty);
    long close_rc = sys_pty_close(shell->ext_pty);
    if (close_rc < 0) {
        return changed;
    }

    int32_t exit_code = 0;
    if (sys_waitpid(shell->ext_pid, &exit_code) < 0) {
        shell_push_text(shell, "run: wait failed");
        changed = true;
    } else {
        shell_push_exit_code(shell, exit_code);
        if (exit_code != 0) changed = true;
    }

    shell_clear_external_state(shell);
    return true;
}

static bool shell_parse_positive_u32(const char* s, uint32_t* out) {
    if (!s || !out || s[0] == '\0') return false;
    uint32_t v = 0;
    for (uint32_t i = 0; s[i]; i++) {
        char c = s[i];
        if (c < '0' || c > '9') return false;
        uint32_t d = (uint32_t)(c - '0');
        if (v > (0xFFFFFFFFu - d) / 10u) return false;
        v = v * 10u + d;
    }
    if (v == 0) return false;
    *out = v;
    return true;
}

static uint32_t shell_history_start(const desktop_shell_t* shell) {
    if (!shell) return 0;
    if (shell->history_count < SHELL_HISTORY_MAX) return 0;
    return shell->history_next;
}

static bool shell_history_get_last(const desktop_shell_t* shell, const char** out_cmd) {
    if (!shell || !out_cmd || shell->history_count == 0) return false;
    uint32_t idx = (shell->history_next + SHELL_HISTORY_MAX - 1) % SHELL_HISTORY_MAX;
    *out_cmd = shell->history[idx];
    return true;
}

static bool shell_history_get_by_number(const desktop_shell_t* shell, uint32_t number, const char** out_cmd) {
    if (!shell || !out_cmd || number == 0 || number > shell->history_count) return false;
    uint32_t idx = (shell_history_start(shell) + (number - 1)) % SHELL_HISTORY_MAX;
    *out_cmd = shell->history[idx];
    return true;
}

static void shell_history_add(desktop_shell_t* shell, const char* cmd) {
    if (!shell || !cmd || cmd[0] == '\0') return;

    const char* prev = NULL;
    if (shell_history_get_last(shell, &prev) && prev && strcmp(prev, cmd) == 0) return;

    shell_copy_cstr(shell->history[shell->history_next], SHELL_INPUT_MAX, cmd);
    shell->history_next = (shell->history_next + 1) % SHELL_HISTORY_MAX;
    if (shell->history_count < SHELL_HISTORY_MAX) {
        shell->history_count++;
    }
}

static bool shell_history_expand(const desktop_shell_t* shell, const char* input, char* out, uint32_t out_size) {
    if (!shell || !input || !out || out_size == 0) return false;
    if (input[0] != '!') return false;

    const char* resolved = NULL;
    if (input[1] == '!') {
        if (input[2] != '\0') return false;
        if (!shell_history_get_last(shell, &resolved)) return false;
    } else {
        uint32_t n = 0;
        if (!shell_parse_positive_u32(input + 1, &n)) return false;
        if (!shell_history_get_by_number(shell, n, &resolved)) return false;
    }

    if (!resolved || resolved[0] == '\0') return false;
    shell_copy_cstr(out, out_size, resolved);
    return true;
}

static void shell_history_dump(desktop_shell_t* shell) {
    if (!shell) return;
    if (shell->history_count == 0) {
        shell_push_text(shell, "(no history)");
        return;
    }

    uint32_t start = shell_history_start(shell);
    for (uint32_t i = 0; i < shell->history_count; i++) {
        uint32_t idx = (start + i) % SHELL_HISTORY_MAX;
        char line[SHELL_LINE_MAX];
        uint32_t pos = 0;
        line[0] = '\0';
        shell_buf_append_u64(line, sizeof(line), &pos, (uint64_t)(i + 1));
        shell_buf_append_str(line, sizeof(line), &pos, "  ");
        shell_buf_append_str(line, sizeof(line), &pos, shell->history[idx]);
        shell_push_text(shell, line);
    }
}

static void shell_push_prompt_command(desktop_shell_t* shell, const char* cmd) {
    if (!shell || !cmd) return;
    char line[SHELL_LINE_MAX];
    uint32_t pos = 0;
    line[0] = '\0';
    if (shell->ext_running) {
        shell_buf_append_str(line, SHELL_LINE_MAX, &pos, "(pty)$ ");
    } else {
        shell_buf_append_char(line, SHELL_LINE_MAX, &pos, '[');
        shell_buf_append_str(line, SHELL_LINE_MAX, &pos, shell->cwd);
        shell_buf_append_str(line, SHELL_LINE_MAX, &pos, "]$ ");
    }
    shell_buf_append_str(line, SHELL_LINE_MAX, &pos, cmd);
    shell_push_scrollback_line(shell, line);
}

static shell_action_t shell_execute_command(desktop_shell_t* shell, char* cmdline) {
    if (!shell || !cmdline || !cmdline[0]) return SHELL_ACTION_NONE;

    char* argv[SHELL_ARG_MAX];
    int argc = shell_parse_args(cmdline, argv, SHELL_ARG_MAX);
    if (argc == 0) return SHELL_ACTION_NONE;

    if (strcmp(argv[0], "help") == 0) {
        shell_push_text(shell, "Built-ins: help clear pwd cd ls dir echo touch mkdir rm run history sysinfo exit quit");
        shell_push_text(shell, "History shortcuts: !!  !N");
        shell_push_text(shell, "External: type ELF name/path or: run <program> [args] (output in this window)");
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

    if (strcmp(argv[0], "history") == 0) {
        shell_history_dump(shell);
        return SHELL_ACTION_NONE;
    }

    if (strcmp(argv[0], "sysinfo") == 0) {
        struct sysinfo info;
        if (sys_sysinfo(&info) < 0) {
            shell_push_text(shell, "sysinfo: failed");
            return SHELL_ACTION_NONE;
        }
        char line[SHELL_LINE_MAX];
        uint32_t pos = 0;
        line[0] = '\0';
        shell_buf_append_str(line, sizeof(line), &pos, "uptime=");
        shell_buf_append_u64(line, sizeof(line), &pos, info.uptime);
        shell_buf_append_str(line, sizeof(line), &pos, " s");
        shell_push_text(shell, line);

        pos = 0;
        line[0] = '\0';
        shell_buf_append_str(line, sizeof(line), &pos, "mem total=");
        shell_buf_append_u64(line, sizeof(line), &pos, info.total_memory);
        shell_buf_append_str(line, sizeof(line), &pos, " free=");
        shell_buf_append_u64(line, sizeof(line), &pos, info.free_memory);
        shell_push_text(shell, line);

        pos = 0;
        line[0] = '\0';
        shell_buf_append_str(line, sizeof(line), &pos, "tasks=");
        shell_buf_append_u64(line, sizeof(line), &pos, info.nr_processes);
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

    if (strcmp(argv[0], "rm") == 0) {
        if (argc < 2) {
            shell_push_text(shell, "Usage: rm <path>");
            return SHELL_ACTION_NONE;
        }
        char path[SHELL_PATH_MAX];
        if (shell_normalize_path(shell, argv[1], path, sizeof(path)) != 0) {
            shell_push_text(shell, "rm: path too long");
            return SHELL_ACTION_NONE;
        }
        if (sys_fs_delete(path) == 0) shell_push_text(shell, "rm: ok");
        else shell_push_text(shell, "rm: failed");
        return SHELL_ACTION_NONE;
    }

    if (strcmp(argv[0], "run") == 0) {
        if (argc < 2) {
            shell_push_text(shell, "Usage: run <program> [args...]");
            return SHELL_ACTION_NONE;
        }

        int exec_rc = shell_start_external_command(shell, argv[1], argc - 2, &argv[2]);
        if (exec_rc == 0) {
            shell_push_text(shell, "run: command not found");
            return SHELL_ACTION_NONE;
        }
        if (exec_rc < 0) return SHELL_ACTION_NONE;

        return SHELL_ACTION_FORCE_FULL_REPAINT;
    }

    if (strcmp(argv[0], "desktop") == 0) {
        shell_push_text(shell, "desktop: already running");
        return SHELL_ACTION_NONE;
    }

    if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) {
        shell_push_text(shell, "Shell window hidden.");
        return SHELL_ACTION_HIDE_WINDOW;
    }

    int exec_rc = shell_start_external_command(shell, argv[0], argc - 1, &argv[1]);
    if (exec_rc > 0) {
        return SHELL_ACTION_FORCE_FULL_REPAINT;
    }
    if (exec_rc < 0) return SHELL_ACTION_NONE;

    shell_push_text(shell, "Unknown command");
    return SHELL_ACTION_NONE;
}

void shell_init(desktop_shell_t* shell) {
    if (!shell) return;
    memset(shell, 0, sizeof(*shell));
    shell_copy_cstr(shell->cwd, SHELL_PATH_MAX, "/");
    shell_copy_cstr(shell->prev_cwd, SHELL_PATH_MAX, "/");
    shell->ext_pid = -1;
    shell->ext_pty = -1;
    shell->ext_running = false;
    for (uint32_t i = 0; i < (uint32_t)(sizeof(k_shell_boot_lines) / sizeof(k_shell_boot_lines[0])); i++) {
        shell_push_scrollback_line(shell, k_shell_boot_lines[i]);
    }
    shell_refresh_view(shell);
}

shell_action_t shell_run_line(desktop_shell_t* shell, const char* line) {
    if (!shell || !line || line[0] == '\0') return SHELL_ACTION_NONE;

    char raw_cmd[SHELL_INPUT_MAX];
    shell_copy_cstr(raw_cmd, sizeof(raw_cmd), line);
    shell_push_prompt_command(shell, raw_cmd);

    char cmd[SHELL_INPUT_MAX];
    shell_copy_cstr(cmd, sizeof(cmd), raw_cmd);
    if (raw_cmd[0] == '!') {
        if (!shell_history_expand(shell, raw_cmd, cmd, sizeof(cmd))) {
            shell_push_text(shell, "history: event not found");
            return SHELL_ACTION_NONE;
        }
        shell_push_text(shell, cmd);
    }

    shell_history_add(shell, cmd);
    return shell_execute_command(shell, cmd);
}

shell_action_t shell_submit_input(desktop_shell_t* shell) {
    if (!shell) return SHELL_ACTION_NONE;

    if (shell->ext_running) {
        char send[SHELL_INPUT_MAX + 2];
        uint32_t n = 0;
        for (uint32_t i = 0; i < shell->input_len && n + 2 < (uint32_t)sizeof(send); i++) {
            send[n++] = shell->input[i];
        }
        send[n++] = '\n';

        if (sys_pty_write(shell->ext_pty, send, n) < 0) {
            shell_push_text(shell, "pty write failed");
        }

        shell->input[0] = '\0';
        shell->input_len = 0;
        return SHELL_ACTION_NONE;
    }

    if (shell->input_len == 0) {
        return SHELL_ACTION_NONE;
    }

    char raw_cmd[SHELL_INPUT_MAX];
    shell_copy_cstr(raw_cmd, sizeof(raw_cmd), shell->input);
    shell->input[0] = '\0';
    shell->input_len = 0;
    return shell_run_line(shell, raw_cmd);
}

void shell_handle_backspace(desktop_shell_t* shell) {
    if (!shell || shell->input_len == 0) return;
    shell->input_len--;
    shell->input[shell->input_len] = '\0';
}

void shell_handle_char(desktop_shell_t* shell, char c) {
    if (!shell) return;
    if ((unsigned char)c < 32 || (unsigned char)c > 126) return;
    if (shell->input_len + 1 >= SHELL_INPUT_MAX) return;
    shell->input[shell->input_len++] = c;
    shell->input[shell->input_len] = '\0';
}
