#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>

#define PTY_OUT_CHUNK 256
#define PTY_CMDLINE_MAX 256

static bool build_cmdline(int argc, char** argv, char* out, uint32_t out_size) {
    if (!out || out_size < 2) {
        return false;
    }

    if (argc <= 1) {
        const char* def = "/SHELL.ELF";
        uint32_t len = (uint32_t)strlen(def);
        if (len + 1 > out_size) return false;
        memcpy(out, def, len + 1);
        return true;
    }

    uint32_t pos = 0;
    out[0] = '\0';
    for (int i = 1; i < argc; i++) {
        const char* part = argv[i];
        if (!part || !part[0]) {
            continue;
        }

        uint32_t part_len = (uint32_t)strlen(part);
        uint32_t need_space = (pos == 0u) ? 0u : 1u;
        if (pos + need_space + part_len + 1u > out_size) {
            return false;
        }

        if (need_space) out[pos++] = ' ';
        memcpy(&out[pos], part, part_len);
        pos += part_len;
        out[pos] = '\0';
    }

    return pos > 0u;
}

int main(int argc, char** argv) {
    char cmdline[PTY_CMDLINE_MAX];
    if (!build_cmdline(argc, argv, cmdline, sizeof(cmdline))) {
        printf("ptysh: command line too long\n");
        sys_exit(1);
        return 1;
    }

    long pty_id = sys_pty_open();
    if (pty_id < 0) {
        printf("ptysh: cannot open PTY\n");
        sys_exit(1);
        return 1;
    }

    long pid = sys_pty_spawn(cmdline, (int32_t)pty_id);
    if (pid < 0) {
        printf("ptysh: spawn failed: %s\n", cmdline);
        (void)sys_pty_close((int32_t)pty_id);
        sys_exit(1);
        return 1;
    }

    printf("ptysh: pid=%ld pty=%ld cmd=\"%s\"\n", pid, pty_id, cmdline);
    printf("ptysh: ESC exits bridge (keyboard polling)\n");

    bool running = true;
    while (running) {
        long out_avail = sys_pty_out_avail((int32_t)pty_id);
        if (out_avail < 0) {
            printf("\nptysh: PTY closed\n");
            break;
        }

        while (out_avail > 0) {
            uint32_t chunk = (out_avail > PTY_OUT_CHUNK) ? PTY_OUT_CHUNK : (uint32_t)out_avail;
            char out_buf[PTY_OUT_CHUNK];
            long n = sys_pty_read((int32_t)pty_id, out_buf, chunk);
            if (n <= 0) break;
            (void)sys_write(out_buf, n);
            out_avail -= n;
        }

        long key = sys_kb_poll();
        if (key > 0) {
            char c = (char)key;
            if ((uint8_t)c == 27u) {
                const char* exit_cmd = "exit\n";
                (void)sys_pty_write((int32_t)pty_id, exit_cmd, 5u);
                running = false;
            } else {
                if (c == '\r') c = '\n';
                (void)sys_pty_write((int32_t)pty_id, &c, 1u);
            }
        } else {
            sys_sleep(10);
        }
    }

    for (int i = 0; i < 40; i++) {
        long out_avail = sys_pty_out_avail((int32_t)pty_id);
        if (out_avail <= 0) break;
        uint32_t chunk = (out_avail > PTY_OUT_CHUNK) ? PTY_OUT_CHUNK : (uint32_t)out_avail;
        char out_buf[PTY_OUT_CHUNK];
        long n = sys_pty_read((int32_t)pty_id, out_buf, chunk);
        if (n <= 0) break;
        (void)sys_write(out_buf, n);
        sys_sleep(10);
    }

    long close_rc = sys_pty_close((int32_t)pty_id);
    if (close_rc < 0) {
        printf("\nptysh: close failed (slave still attached)\n");
    }

    sys_exit(0);
    return 0;
}
