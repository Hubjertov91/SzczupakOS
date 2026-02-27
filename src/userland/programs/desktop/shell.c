#include "desktop.h"

static const char* k_shell_boot_lines[] = {
    "SzczupakOS Shell (window mode)",
    "Type: help"
};

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
        shell_push_text(shell, "Built-ins: help clear pwd cd ls dir echo touch mkdir rm exit quit");
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

void shell_init(desktop_shell_t* shell) {
    if (!shell) return;
    memset(shell, 0, sizeof(*shell));
    shell_copy_cstr(shell->cwd, SHELL_PATH_MAX, "/");
    shell_copy_cstr(shell->prev_cwd, SHELL_PATH_MAX, "/");
    for (uint32_t i = 0; i < (uint32_t)(sizeof(k_shell_boot_lines) / sizeof(k_shell_boot_lines[0])); i++) {
        shell_push_scrollback_line(shell, k_shell_boot_lines[i]);
    }
    shell_refresh_view(shell);
}

shell_action_t shell_submit_input(desktop_shell_t* shell) {
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
