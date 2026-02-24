#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <stddef.h>
#include <stdbool.h>

#define MAX_CMD_LEN 512
#define MAX_ARGS 32
#define MAX_PATH 256
#define MAX_HISTORY 16

static char cwd[MAX_PATH] = "/";
static char line_buf[MAX_CMD_LEN];
static char* argv[MAX_ARGS];
static int argc;
static char history[MAX_HISTORY][MAX_CMD_LEN];
static int history_count = 0;
static int history_next = 0;

static long try_exec_command(const char* cmd);

int cmd_help(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Built-in commands:\n");
    printf("  help        - Show this help\n");
    printf("  echo        - Print arguments\n");
    printf("  history     - Show command history\n");
    printf("  dir         - List current directory\n");
    printf("  ls          - Alias for dir\n");
    printf("  pwd         - Print working directory\n");
    printf("  cd          - Change directory\n");
    printf("  run         - Run external program\n");
    printf("  clear       - Clear screen\n");
    printf("  sysinfo     - Show system info\n");
    printf("  net         - Show network configuration\n");
    printf("  ping        - Send ICMP ping (ping <ip> [count])\n");
    printf("  exit        - Exit shell\n");
    printf("  quit        - Exit shell (alias)\n");
    printf("\n");
    printf("External programs:\n");
    printf("  Type ELF name or path, e.g. /LS.ELF\n");
    return 0;
}

int cmd_echo(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        printf("%s", argv[i]);
        if (i < argc - 1) printf(" ");
    }
    printf("\n");
    return 0;
}

int cmd_pwd(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("%s\n", cwd);
    return 0;
}

static int normalize_path(const char* input, char* out, size_t out_size) {
    if (!input || !out || out_size < 2) return -1;

    char combined[MAX_PATH];
    if (input[0] == '/') {
        if ((size_t)strlen(input) >= sizeof(combined)) return -1;
        strcpy(combined, input);
    } else {
        size_t cwd_len = (size_t)strlen(cwd);
        size_t input_len = (size_t)strlen(input);
        size_t need_slash = (cwd_len > 0 && cwd[cwd_len - 1] != '/') ? 1 : 0;
        if (cwd_len + need_slash + input_len >= sizeof(combined)) return -1;
        strcpy(combined, cwd);
        if (need_slash) strcat(combined, "/");
        strcat(combined, input);
    }

    size_t out_len = 1;
    out[0] = '/';
    out[1] = '\0';

    size_t i = 0;
    while (combined[i] != '\0') {
        while (combined[i] == '/') i++;
        if (combined[i] == '\0') break;

        char segment[MAX_PATH];
        size_t seg_len = 0;
        while (combined[i] != '\0' && combined[i] != '/') {
            if (seg_len + 1 >= sizeof(segment)) return -1;
            segment[seg_len++] = combined[i++];
        }
        segment[seg_len] = '\0';

        if (seg_len == 1 && segment[0] == '.') {
            continue;
        }

        if (seg_len == 2 && segment[0] == '.' && segment[1] == '.') {
            if (out_len > 1) {
                size_t p = out_len;
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

        for (size_t j = 0; j < seg_len; j++) {
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

static void history_add(const char* line) {
    if (!line || !line[0]) return;

    if (history_count > 0) {
        int last = (history_next + MAX_HISTORY - 1) % MAX_HISTORY;
        if (strcmp(history[last], line) == 0) return;
    }

    size_t len = (size_t)strlen(line);
    if (len >= MAX_CMD_LEN) len = MAX_CMD_LEN - 1;

    memcpy(history[history_next], line, len);
    history[history_next][len] = '\0';

    history_next = (history_next + 1) % MAX_HISTORY;
    if (history_count < MAX_HISTORY) history_count++;
}

int cmd_history(int argc, char** argv) {
    (void)argc; (void)argv;

    if (history_count == 0) {
        printf("(no history)\n");
        return 0;
    }

    int start = (history_count < MAX_HISTORY) ? 0 : history_next;
    for (int i = 0; i < history_count; i++) {
        int idx = (start + i) % MAX_HISTORY;
        printf("%d  %s\n", i + 1, history[idx]);
    }
    return 0;
}

int cmd_cd(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cd <path>\n");
        return 1;
    }

    char normalized[MAX_PATH];
    if (normalize_path(argv[1], normalized, sizeof(normalized)) != 0) {
        printf("Path too long\n");
        return 1;
    }

    strcpy(cwd, normalized);
    return 0;
}

int cmd_dir(int argc, char** argv) {
    (void)argc; (void)argv;

    char output[2048];
    long n = sys_listdir(cwd, output, sizeof(output) - 1);
    if (n < 0) {
        printf("dir: failed to list %s\n", cwd);
        return 1;
    }
    if (n > 0) {
        sys_write(output, n);
    }
    return 0;
}

int cmd_run(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: run <program>\n");
        return 1;
    }

    long pid = try_exec_command(argv[1]);
    if (pid < 0) {
        printf("Command not found: %s\n", argv[1]);
        return 1;
    }

    sys_sleep(10);
    return 0;
}

int cmd_clear(int argc, char** argv) {
    (void)argc; (void)argv;
    sys_clear();
    printf("SzczupakOS Shell\n");
    printf("Type 'help' for available commands\n\n");
    return 0;
}

int cmd_sysinfo(int argc, char** argv) {
    (void)argc; (void)argv;
    struct sysinfo info;
    if (sys_sysinfo(&info) < 0) {
        printf("Error getting system info\n");
        return 1;
    }
    
    printf("=== System Information ===\n");
    printf("Uptime: %lu seconds\n", info.uptime);
    printf("Total Memory: %lu bytes\n", info.total_memory);
    printf("Free Memory: %lu bytes\n", info.free_memory);
    printf("Running Processes: %u\n", info.nr_processes);
    return 0;
}

static char hex_nibble(uint8_t value) {
    return (value < 10) ? (char)('0' + value) : (char)('A' + (value - 10));
}

static void print_hex_byte(uint8_t value) {
    putchar(hex_nibble((uint8_t)(value >> 4)));
    putchar(hex_nibble((uint8_t)(value & 0x0F)));
}

static void print_mac(const uint8_t mac[6]) {
    for (int i = 0; i < 6; i++) {
        print_hex_byte(mac[i]);
        if (i != 5) putchar(':');
    }
}

static void print_ip4(const uint8_t ip[4]) {
    printf("%u.%u.%u.%u", (unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3]);
}

static int parse_ipv4(const char* s, uint8_t out[4]) {
    if (!s || !out) return -1;

    const char* p = s;
    for (int part = 0; part < 4; part++) {
        if (*p < '0' || *p > '9') return -1;
        unsigned value = 0;
        int digits = 0;

        while (*p >= '0' && *p <= '9') {
            value = value * 10 + (unsigned)(*p - '0');
            if (value > 255) return -1;
            p++;
            digits++;
        }

        if (digits == 0) return -1;
        out[part] = (uint8_t)value;

        if (part < 3) {
            if (*p != '.') return -1;
            p++;
        }
    }

    return (*p == '\0') ? 0 : -1;
}

int cmd_net(int argc, char** argv) {
    (void)argc;
    (void)argv;

    struct net_info info;
    if (sys_net_info(&info) < 0) {
        printf("net: unavailable\n");
        return 1;
    }

    printf("=== Network ===\n");
    printf("Link: %s\n", info.link_up ? "up" : "down");
    printf("DHCP: %s\n", info.configured ? "configured" : "not configured");
    printf("MAC: ");
    print_mac(info.mac);
    printf("\n");
    printf("IP: ");
    print_ip4(info.ip);
    printf("\n");
    printf("MASK: ");
    print_ip4(info.netmask);
    printf("\n");
    printf("GW: ");
    print_ip4(info.gateway);
    printf("\n");
    printf("DNS: ");
    print_ip4(info.dns);
    printf("\n");
    printf("Lease: %u s\n", (unsigned)info.lease_time_seconds);
    return 0;
}

int cmd_ping(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: ping <ip> [count]\n");
        return 1;
    }

    uint8_t ip[4];
    if (parse_ipv4(argv[1], ip) != 0) {
        printf("ping: invalid IPv4 address\n");
        return 1;
    }

    int count = 4;
    if (argc >= 3) {
        long v = atoi(argv[2]);
        if (v <= 0 || v > 20) {
            printf("ping: count must be 1..20\n");
            return 1;
        }
        count = (int)v;
    }

    printf("PING ");
    print_ip4(ip);
    printf(":\n");

    int success = 0;
    for (int i = 0; i < count; i++) {
        uint32_t rtt_ms = 0;
        if (sys_net_ping(ip, 1000, &rtt_ms) == 0) {
            printf("  reply from ");
            print_ip4(ip);
            printf(": time=%u ms\n", (unsigned)rtt_ms);
            success++;
        } else {
            printf("  request timeout\n");
        }
        if (i + 1 < count) sys_sleep(250);
    }

    printf("Ping statistics: %d/%d replies\n", success, count);
    return (success > 0) ? 0 : 1;
}

int cmd_exit(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Goodbye!\n");
    sys_exit(0);
    return 0;
}

static char upper_ascii(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - ('a' - 'A'));
    return c;
}

static int build_abs_path(const char* cmd, char* out, size_t out_size) {
    if (!cmd || !out || out_size == 0) return -1;
    size_t cmd_len = (size_t)strlen(cmd);
    if (cmd_len == 0) return -1;

    if (cmd[0] == '/') {
        if (cmd_len >= out_size) return -1;
        strcpy(out, cmd);
        return 0;
    }

    size_t cwd_len = (size_t)strlen(cwd);
    size_t need_slash = (cwd_len > 0 && cwd[cwd_len - 1] != '/') ? 1 : 0;
    if (cwd_len + need_slash + cmd_len >= out_size) return -1;

    size_t pos = 0;
    for (size_t i = 0; i < cwd_len; i++) out[pos++] = cwd[i];
    if (need_slash) out[pos++] = '/';
    for (size_t i = 0; i < cmd_len; i++) out[pos++] = cmd[i];
    out[pos] = '\0';
    return 0;
}

static bool basename_has_dot(const char* path) {
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

static int append_elf_ext(const char* in, char* out, size_t out_size) {
    if (!in || !out || out_size == 0) return -1;
    size_t len = (size_t)strlen(in);
    if (len + 4 >= out_size) return -1;
    strcpy(out, in);
    strcat(out, ".ELF");
    return 0;
}

static void to_upper_path(const char* in, char* out, size_t out_size) {
    if (!in || !out || out_size == 0) return;
    size_t i = 0;
    while (in[i] && i + 1 < out_size) {
        out[i] = upper_ascii(in[i]);
        i++;
    }
    out[i] = '\0';
}

static long try_exec_command(const char* cmd) {
    char abs_path[MAX_PATH];
    if (build_abs_path(cmd, abs_path, sizeof(abs_path)) != 0) return -1;

    long pid = sys_exec(abs_path);
    if (pid >= 0) return pid;

    char upper_path[MAX_PATH];
    to_upper_path(abs_path, upper_path, sizeof(upper_path));
    if (strcmp(upper_path, abs_path) != 0) {
        pid = sys_exec(upper_path);
        if (pid >= 0) return pid;
    }

    if (!basename_has_dot(abs_path)) {
        char abs_elf[MAX_PATH];
        if (append_elf_ext(abs_path, abs_elf, sizeof(abs_elf)) == 0) {
            pid = sys_exec(abs_elf);
            if (pid >= 0) return pid;

            char upper_elf[MAX_PATH];
            to_upper_path(abs_elf, upper_elf, sizeof(upper_elf));
            if (strcmp(upper_elf, abs_elf) != 0) {
                pid = sys_exec(upper_elf);
                if (pid >= 0) return pid;
            }
        }
    }

    return -1;
}

void parse_line(char* line) {
    argc = 0;
    char* token = strtok(line, " \t\n\r");
    
    while (token && argc < MAX_ARGS - 1) {
        argv[argc++] = token;
        token = strtok(NULL, " \t\n\r");
    }
    argv[argc] = NULL;
}

int execute_command(char* line) {
    if (!line || !*line) return 0;
    
    parse_line(line);
    
    if (argc == 0) return 0;

    uintptr_t line_start = (uintptr_t)line;
    uintptr_t line_end = line_start + MAX_CMD_LEN;
    uintptr_t cmd_ptr = (uintptr_t)argv[0];
    if (cmd_ptr < line_start || cmd_ptr >= line_end) {
        printf("Parse error\n");
        return 1;
    }
    
    const char* cmd = argv[0];
    
    if (strcmp(cmd, "help") == 0) return cmd_help(argc, argv);
    if (strcmp(cmd, "echo") == 0) return cmd_echo(argc, argv);
    if (strcmp(cmd, "history") == 0) return cmd_history(argc, argv);
    if (strcmp(cmd, "dir") == 0) return cmd_dir(argc, argv);
    if (strcmp(cmd, "ls") == 0) return cmd_dir(argc, argv);
    if (strcmp(cmd, "pwd") == 0) return cmd_pwd(argc, argv);
    if (strcmp(cmd, "cd") == 0) return cmd_cd(argc, argv);
    if (strcmp(cmd, "run") == 0) return cmd_run(argc, argv);
    if (strcmp(cmd, "clear") == 0) return cmd_clear(argc, argv);
    if (strcmp(cmd, "sysinfo") == 0) return cmd_sysinfo(argc, argv);
    if (strcmp(cmd, "net") == 0) return cmd_net(argc, argv);
    if (strcmp(cmd, "ifconfig") == 0) return cmd_net(argc, argv);
    if (strcmp(cmd, "ping") == 0) return cmd_ping(argc, argv);
    if (strcmp(cmd, "exit") == 0) return cmd_exit(argc, argv);
    if (strcmp(cmd, "quit") == 0) return cmd_exit(argc, argv);

    long pid = try_exec_command(cmd);
    if (pid >= 0) {
        sys_sleep(10);
        return 0;
    }
    
    printf("Unknown command: %s\n", cmd);
    return 1;
}

int main(void) {
    sys_clear();
    printf("SzczupakOS Shell v1.0\n");
    printf("Type 'help' for available commands\n\n");

    while (1) {
        sys_write("[", 1);
        sys_write(cwd, strlen(cwd));
        sys_write("]$ ", 3);
        
        memset(line_buf, 0, MAX_CMD_LEN);
        long len = sys_read(line_buf, MAX_CMD_LEN - 1);
        
        if (len <= 0) {
            sys_sleep(100);
            continue;
        }

        if (len >= MAX_CMD_LEN) {
            line_buf[MAX_CMD_LEN - 1] = '\0';
            continue;
        }
        
        if (len > 0 && line_buf[len - 1] == '\n') {
            line_buf[len - 1] = '\0';
        }

        history_add(line_buf);
        
        execute_command(line_buf);
    }
    
    return 0;
}
