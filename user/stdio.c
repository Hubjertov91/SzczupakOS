#include "stdint.h"
#include "stdlib.h"
#include "syscall.h"
#include "stdarg.h"
#include "stdio.h"

FILE _stdin = {0};
FILE _stdout = {0};

FILE* stdin = &_stdin;
FILE* stdout = &_stdout;

int putchar(char c) {
    sys_write(&c, 1);
    return c;
}

static int write_str(const char* s) {
    int len = 0;
    while (s[len]) len++;
    sys_write(s, len);
    return len;
}

static int write_hex(unsigned long val, int upper) {
    if (val == 0) {
        sys_write("0", 1);
        return 1;
    }
    char tmp[32];
    int i = 0;
    while (val > 0) {
        int d = val & 0xf;
        if (d < 10)
            tmp[i++] = '0' + d;
        else
            tmp[i++] = (upper ? 'A' : 'a') + (d - 10);
        val >>= 4;
    }
    int written = i;
    while (i--) sys_write(&tmp[i], 1);
    return written;
}

int printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int total = 0;
    for (const char* p = fmt; *p; p++) {
        if (*p == '%') {
            p++;
            int is_long = 0;
            if (*p == 'l') {
                is_long = 1;
                p++;
            }
            if (*p == 'd') {
                long val = is_long ? va_arg(ap, long) : (long)va_arg(ap, int);
                char num[32];
                itoa(val, num);
                total += write_str(num);
            } else if (*p == 'u') {
                unsigned long val = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
                char num[32];
                uitoa(val, num);
                total += write_str(num);
            } else if (*p == 'x') {
                unsigned long val = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
                total += write_hex(val, 0);
            } else if (*p == 'X') {
                unsigned long val = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
                total += write_hex(val, 1);
            } else if (*p == 'c') {
                char c = (char)va_arg(ap, int);
                sys_write(&c, 1);
                total += 1;
            } else if (*p == 's') {
                char* s = va_arg(ap, char*);
                total += write_str(s);
            } else if (*p == '%') {
                sys_write("%", 1);
                total += 1;
            } else {
                sys_write("%", 1);
                sys_write(p, 1);
                total += 2;
            }
        } else {
            sys_write(p, 1);
            total += 1;
        }
    }
    va_end(ap);
    return total;
}

char* fgets(char* buf, int n, FILE* f) {
    (void)f;
    int i = 0;
    char c;
    while (i < n - 1) {
        if (sys_read(&c, 1) <= 0) break;
        buf[i++] = c;
        if (c == '\n') break;
    }
    buf[i] = '\0';
    if (i == 0) return NULL;
    return buf;
}