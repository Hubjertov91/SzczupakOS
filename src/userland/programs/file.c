#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <syscall.h>

#define FILE_PATH_MAX 256
#define FILE_PROBE_MAX 512

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

static bool bytes_eq(const uint8_t* data, long size, const uint8_t* sig, long sig_len) {
    if (!data || !sig || sig_len <= 0 || size < sig_len) return false;
    return memcmp(data, sig, sig_len) == 0;
}

static bool is_probably_text(const uint8_t* data, long size) {
    if (!data || size <= 0) return false;

    long printable = 0;
    for (long i = 0; i < size; i++) {
        uint8_t c = data[i];
        if (c == 0) return false;
        if (c == '\n' || c == '\r' || c == '\t') {
            printable++;
            continue;
        }
        if (c >= 32 && c <= 126) {
            printable++;
        }
    }

    return (printable * 100) / size >= 90;
}

static const char* detect_format(const uint8_t* data, long size) {
    static const uint8_t k_png[] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    static const uint8_t k_jpg[] = {0xFF, 0xD8, 0xFF};
    static const uint8_t k_bmp[] = {'B', 'M'};
    static const uint8_t k_gif87[] = {'G', 'I', 'F', '8', '7', 'a'};
    static const uint8_t k_gif89[] = {'G', 'I', 'F', '8', '9', 'a'};
    static const uint8_t k_pdf[] = {'%', 'P', 'D', 'F', '-'};
    static const uint8_t k_elf[] = {0x7F, 'E', 'L', 'F'};
    static const uint8_t k_psf1[] = {0x36, 0x04};
    static const uint8_t k_psf2[] = {0x72, 0xB5, 0x4A, 0x86};
    static const uint8_t k_zip1[] = {'P', 'K', 0x03, 0x04};
    static const uint8_t k_zip2[] = {'P', 'K', 0x05, 0x06};
    static const uint8_t k_zip3[] = {'P', 'K', 0x07, 0x08};

    if (size == 0) return "empty";
    if (bytes_eq(data, size, k_elf, sizeof(k_elf))) return "ELF executable";
    if (bytes_eq(data, size, k_psf1, sizeof(k_psf1))) return "PSF1 font";
    if (bytes_eq(data, size, k_psf2, sizeof(k_psf2))) return "PSF2 font";
    if (bytes_eq(data, size, k_png, sizeof(k_png))) return "PNG image";
    if (bytes_eq(data, size, k_jpg, sizeof(k_jpg))) return "JPEG image";
    if (bytes_eq(data, size, k_bmp, sizeof(k_bmp))) return "BMP image";
    if (bytes_eq(data, size, k_gif87, sizeof(k_gif87)) || bytes_eq(data, size, k_gif89, sizeof(k_gif89))) return "GIF image";
    if (bytes_eq(data, size, k_pdf, sizeof(k_pdf))) return "PDF document";
    if (bytes_eq(data, size, k_zip1, sizeof(k_zip1)) ||
        bytes_eq(data, size, k_zip2, sizeof(k_zip2)) ||
        bytes_eq(data, size, k_zip3, sizeof(k_zip3))) return "ZIP archive";
    if (size >= 262 && memcmp(data + 257, "ustar", 5) == 0) return "TAR archive";
    if (is_probably_text(data, size)) return "text";
    return "binary (unknown)";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("Usage: file <path> [path...]\n");
        printf("Detects format by signature (magic bytes).\n");
        sys_exit(1);
        return 1;
    }

    int rc = 0;
    for (int i = 1; i < argc; i++) {
        char path[FILE_PATH_MAX];
        if (to_abs_path(argv[i], path, sizeof(path)) != 0) {
            printf("file: invalid path: %s\n", argv[i]);
            rc = 1;
            continue;
        }

        uint8_t probe[FILE_PROBE_MAX];
        long n = sys_fs_read(path, 0, (char*)probe, sizeof(probe));
        if (n < 0) {
            printf("file: cannot read: %s\n", path);
            rc = 1;
            continue;
        }

        const char* fmt = detect_format(probe, n);
        printf("%s: %s\n", path, fmt);
    }

    sys_exit(rc);
    return rc;
}
