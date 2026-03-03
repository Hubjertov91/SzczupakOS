#include <stdio.h>
#include <string.h>
#include <syscall.h>

#define CAT_PATH_MAX 256
#define CAT_BUF_SIZE 4096

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

static int cat_one(const char* user_path) {
    char path[CAT_PATH_MAX];
    if (to_abs_path(user_path, path, sizeof(path)) != 0) {
        printf("cat: invalid path: %s\n", user_path);
        return 1;
    }

    char buf[CAT_BUF_SIZE];
    uint64_t offset = 0;

    while (1) {
        long n = sys_fs_read(path, offset, buf, sizeof(buf));
        if (n < 0) {
            printf("cat: cannot read: %s\n", path);
            return 1;
        }
        if (n == 0) {
            break;
        }

        long written = sys_write(buf, n);
        if (written != n) {
            printf("cat: write error\n");
            return 1;
        }
        offset += (uint64_t)n;
    }

    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: cat <path> [path...]\n");
        printf("Print file content to stdout.\n");
        sys_exit(1);
        return 1;
    }

    int rc = 0;
    for (int i = 1; i < argc; i++) {
        if (cat_one(argv[i]) != 0) {
            rc = 1;
        }
    }

    sys_exit(rc);
    return rc;
}
