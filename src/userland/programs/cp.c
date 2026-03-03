#include <stdio.h>
#include <string.h>
#include <syscall.h>

#define CP_PATH_MAX 256
#define CP_BUF_SIZE 1024

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
    if (argc != 3) {
        printf("Usage: cp <source> <dest>\n");
        printf("Note: relative paths are treated as /<name>\n");
        sys_exit(1);
        return 1;
    }

    char src[CP_PATH_MAX];
    char dst[CP_PATH_MAX];

    if (to_abs_path(argv[1], src, sizeof(src)) != 0 ||
        to_abs_path(argv[2], dst, sizeof(dst)) != 0) {
        printf("cp: invalid path\n");
        sys_exit(1);
        return 1;
    }

    if (strcmp(src, dst) == 0) {
        printf("cp: source and destination are the same\n");
        sys_exit(1);
        return 1;
    }

    long del = sys_fs_delete(dst);
    if (del != 0 && del != -6) {
        printf("cp: cannot replace destination: %s\n", dst);
        sys_exit(1);
        return 1;
    }

    char buf[CP_BUF_SIZE];
    uint64_t off = 0;

    for (;;) {
        long n = sys_fs_read(src, off, buf, sizeof(buf));
        if (n < 0) {
            printf("cp: read failed: %s\n", src);
            sys_exit(1);
            return 1;
        }
        if (n == 0) {
            break;
        }

        long w = sys_fs_write(dst, off, buf, (uint32_t)n);
        if (w != n) {
            printf("cp: write failed: %s\n", dst);
            sys_exit(1);
            return 1;
        }

        off += (uint64_t)n;
    }

    sys_exit(0);
    return 0;
}
