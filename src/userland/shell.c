#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <stddef.h>

static uint32_t cursor_x = 10;
static uint32_t cursor_y = 10;
static struct fb_info fb_info;
static int fb_available = 0;

#define CHAR_WIDTH 8
#define CHAR_HEIGHT 16
#define FG_COLOR 0xFFFFFF
#define BG_COLOR 0x0064FF

void init_fb(void) {
    if (sys_fb_info(&fb_info) == 0) {
        fb_available = 1;
        sys_fb_clear(BG_COLOR);
    }
}

void fb_putchar_at(uint32_t x, uint32_t y, char c) {
    if (!fb_available) return;
    sys_fb_putchar_psf(x, y, c, FG_COLOR, BG_COLOR);
}

void fb_putchar(char c) {
    if (!fb_available) {
        putchar(c);
        return;
    }
    
    if (c == '\n') {
        cursor_x = 10;
        cursor_y += CHAR_HEIGHT;
        if (cursor_y + CHAR_HEIGHT > fb_info.height) {
            cursor_y = 10;
            sys_fb_clear(BG_COLOR);
        }
        return;
    }
    
    if (c == '\b') {
        if (cursor_x > 10) {
            cursor_x -= CHAR_WIDTH;
            fb_putchar_at(cursor_x, cursor_y, ' ');
        }
        return;
    }
    
    fb_putchar_at(cursor_x, cursor_y, c);
    cursor_x += CHAR_WIDTH;
    
    if (cursor_x + CHAR_WIDTH > fb_info.width) {
        cursor_x = 10;
        cursor_y += CHAR_HEIGHT;
        if (cursor_y + CHAR_HEIGHT > fb_info.height) {
            cursor_y = 10;
            sys_fb_clear(BG_COLOR);
        }
    }
}

void fb_print(const char* str) {
    if (!fb_available) {
        printf("%s", str);
        return;
    }
    
    while (*str) {
        fb_putchar(*str++);
    }
}

void cmd_fbtest(void) {
    if (!fb_available) {
        fb_print("Framebuffer not available\n");
        return;
    }
    
    sys_fb_clear(0x000000);
    
    for (int i = 0; i < 100; i++) {
        uint32_t color = (i * 2) << 16 | (255 - i * 2) << 8 | 128;
        sys_fb_rect(100 + i, 100, 5, 200, color);
    }
    
    sys_sleep(2000);
    sys_fb_clear(BG_COLOR);
    cursor_x = 10;
    cursor_y = 10;
    fb_print("Test complete!\n");
}

#define MAX_CMD_LEN 128
#define MAX_ARGS    8
int readline(char* buf, int n);

static void print_prompt(void) {
    fb_print("SzczupakOS> ");
}

static void cmd_help(void) {
    fb_print("Available commands:\n");
    fb_print("  help       - show this message\n");
    fb_print("  echo args  - print args\n");
    fb_print("  pid        - show current PID\n");
    fb_print("  uptime     - show system uptime\n");
    fb_print("  clear      - clear screen\n");
    fb_print("  fbtest     - framebuffer test\n");
    fb_print("  exit       - exit shell\n");
}

int readline(char* buf, int n) {
    int pos = 0;
    char c;
    while (1) {
        if (sys_read(&c, 1) <= 0) continue; 
        if (c == '\n') {
            buf[pos++] = '\n';
            buf[pos] = 0;
            fb_putchar('\n');
            return pos;
        }
        if (c == '\b' || c == 127) {
            if (pos > 0) {
                pos--;
                fb_putchar('\b');
            }
            continue;
        }
        if (pos < n - 1) {
            buf[pos++] = c;
            fb_putchar(c);
        }
    }
}

static void cmd_echo(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        fb_print(argv[i]);
        fb_print(" ");
    }
    fb_putchar('\n');
}

static void cmd_pid(void) {
    char buf[32];
    long pid = sys_getpid();
    fb_print("PID: ");
    itoa(pid, buf);
    fb_print(buf);
    fb_putchar('\n');
}

static void cmd_uptime(void) {
    char buf[32];
    long uptime = sys_gettime();
    fb_print("Uptime: ");
    itoa(uptime, buf);
    fb_print(buf);
    fb_print(" ticks\n");
}

static void cmd_clear(void) {
    if (fb_available) {
        sys_fb_clear(BG_COLOR);
        cursor_x = 10;
        cursor_y = 10;
    } else {
        sys_clear();
    }
}

static void cmd_exit(void) {
    fb_print("Goodbye!\n");
    sys_exit(0);
}

static int parse_command(char* input, char** argv) {
    if (!input || !argv) return 0;
    
    int argc = 0;
    char* tok = strtok(input, " \t\n");
    while (tok != NULL && argc < MAX_ARGS) {
        argv[argc++] = tok;
        tok = strtok(NULL, " \t\n");
    }
    argv[argc] = NULL;
    return argc;
}

int main(void) {
    char line[MAX_CMD_LEN];
    char* argv[MAX_ARGS + 1];
    int argc;
    
    init_fb();
    cmd_clear();
    fb_print("Welcome to SzczupakOS Shell\n");

    while (1) {
        print_prompt();
        readline(line, sizeof(line));

        argc = parse_command(line, argv);
        if (argc == 0) continue;

        if (strcmp(argv[0], "help") == 0) cmd_help();
        else if (strcmp(argv[0], "echo") == 0) cmd_echo(argc, argv);
        else if (strcmp(argv[0], "pid") == 0) cmd_pid();
        else if (strcmp(argv[0], "uptime") == 0) cmd_uptime();
        else if (strcmp(argv[0], "clear") == 0) cmd_clear();
        else if (strcmp(argv[0], "exit") == 0) cmd_exit();
        else if (strcmp(argv[0], "fbtest") == 0) cmd_fbtest();
        else {
            fb_print("Unknown command: ");
            fb_print(argv[0]);
            fb_putchar('\n');
        }
    }

    return 0;
}