#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <syscall.h>

#define MKDIR_PATH_MAX 256

static int to_abs_path(const char* in, char* out, long out_size) {
    if (!in || !out || out_size < 2) return -1;

    if (in[0] == '/') {
        long len = strlen(in);
        if (len <= 0 || len >= out_size) return -1;
        strcpy(out, in);
        return 0;
    }

    long len = strlen(in);
    if (len <= 0 || (len + 1) >= out_size) return -1;
    out[0] = '/';
    strcpy(out + 1, in);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: mkdir [-p] <path> [path...]\n");
        printf("Note: relative paths are treated as /<name>\n");
        sys_exit(1);
        return 1;
    }

    bool opt_p = false;
    int first_path = 1;
    if (argc >= 2 && strcmp(argv[1], "-p") == 0) {
        opt_p = true;
        first_path = 2;
    }

    if (first_path >= argc) {
        printf("mkdir: missing operand\n");
        sys_exit(1);
        return 1;
    }

    int rc = 0;
    for (int i = first_path; i < argc; i++) {
        char path[MKDIR_PATH_MAX];
        if (to_abs_path(argv[i], path, sizeof(path)) != 0) {
            printf("mkdir: invalid path: %s\n", argv[i]);
            rc = 1;
            continue;
        }

        long ret = sys_fs_mkdir(path);
        if (ret == 0) {
            continue;
        } else if (ret == -3 && opt_p) {
            continue;
        } else if (ret == -1) {
            printf("mkdir: invalid path: %s\n", argv[i]);
        } else if (ret == -2) {
            printf("mkdir: parent directory not found: %s\n", argv[i]);
        } else if (ret == -3) {
            printf("mkdir: file exists: %s\n", argv[i]);
        } else if (ret == -4) {
            printf("mkdir: only root directory is currently supported: %s\n", argv[i]);
        } else {
            printf("mkdir: failed to create directory: %s\n", argv[i]);
        }
        rc = 1;
    }

    sys_exit(rc);
    return rc;
}
