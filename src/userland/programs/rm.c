#include <stdio.h>
#include <string.h>
#include <syscall.h>

#define RM_PATH_MAX 256

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
        printf("Usage: rm <path> [path...]\n");
        printf("Note: relative paths are treated as /<name>\n");
        sys_exit(1);
        return 1;
    }

    int rc = 0;
    for (int i = 1; i < argc; i++) {
        char path[RM_PATH_MAX];
        if (to_abs_path(argv[i], path, sizeof(path)) != 0) {
            printf("rm: invalid path: %s\n", argv[i]);
            rc = 1;
            continue;
        }

        long ret = sys_fs_delete(path);
        if (ret == 0) {
            continue;
        } else if (ret == -1) {
            printf("rm: invalid path: %s\n", argv[i]);
        } else if (ret == -2) {
            printf("rm: parent directory not found: %s\n", argv[i]);
        } else if (ret == -6) {
            printf("rm: no such file or directory: %s\n", argv[i]);
        } else if (ret == -7) {
            printf("rm: cannot remove (not empty or unsupported): %s\n", argv[i]);
        } else {
            printf("rm: failed to remove: %s\n", argv[i]);
        }
        rc = 1;
    }

    sys_exit(rc);
    return rc;
}
