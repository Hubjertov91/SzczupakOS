#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <stddef.h>

#define MAX_CMD_LEN 128
#define MAX_ARGS    8
int readline(char* buf, int n);

static void print_prompt(void) {
    printf("SzczupakOS> ");
}

static void cmd_help(void) {
    printf("Available commands:\n");
    printf("  help       - show this message\n");
    printf("  echo args  - print args\n");
    printf("  pid        - show current PID\n");
    printf("  uptime     - show system uptime\n");
    printf("  clear      - clear screen\n");
    printf("  exit       - exit shell\n");
}

int readline(char* buf, int n) {
    int pos = 0;
    char c;
    while (1) {
        if (sys_read(&c, 1) <= 0) continue; 
        if (c == '\n') {
            buf[pos++] = '\n';
            buf[pos] = 0;
            putchar('\n'); 
            return pos;
        }
        if (c == '\b' || c == 127) {
            if (pos > 0) {
                pos--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
            continue;
        }
        if (pos < n - 1) {
            buf[pos++] = c;
            putchar(c); 
        }
    }
}

static void cmd_echo(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        printf("%s ", argv[i]);
    }
    printf("\n");
}

static void cmd_pid(void) {
    printf("PID: %ld\n", sys_getpid());
}

static void cmd_uptime(void) {
    printf("Uptime: %ld ticks\n", sys_gettime());
}

static void cmd_clear(void) {
    sys_clear();
}

static void cmd_exit(void) {
    printf("Goodbye!\n");
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
    
    sys_clear();
    printf("Welcome to SzczupakOS Shell\n");

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
        else printf("Unknown command: %s\n", argv[0]);
    }

    return 0;
}
