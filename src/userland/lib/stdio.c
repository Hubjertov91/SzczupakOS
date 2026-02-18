#include <stdint.h>
#include <stdlib.h>
#include <syscall.h>
#include <stdarg.h>
#include <stdio.h>

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
        tmp[i++] = (d < 10) ? ('0' + d) : ((upper ? 'A' : 'a') + (d - 10));
        val >>= 4;
    }
    int written = i;
    while (i--) sys_write(&tmp[i], 1);
    return written;
}

int printf(const char* fmt, ...) {
    static char buffer[1024];
    int buf_pos = 0;
    
    va_list ap;
    va_start(ap, fmt);
    
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
                for (char* n = num; *n; n++) {
                    if (buf_pos < 1023) buffer[buf_pos++] = *n;
                }
            } else if (*p == 'u') {
                unsigned long val = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
                char num[32];
                uitoa(val, num);
                for (char* n = num; *n; n++) {
                    if (buf_pos < 1023) buffer[buf_pos++] = *n;
                }
            } else if (*p == 'x' || *p == 'X') {
                unsigned long val = is_long ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
                char hex[32];
                int i = 0;
                if (val == 0) {
                    hex[i++] = '0';
                } else {
                    char tmp[32];
                    int j = 0;
                    while (val > 0) {
                        int d = val & 0xf;
                        tmp[j++] = (d < 10) ? ('0' + d) : ((*p == 'X') ? 'A' : 'a') + (d - 10);
                        val >>= 4;
                    }
                    while (j--) hex[i++] = tmp[j];
                }
                for (int k = 0; k < i; k++) {
                    if (buf_pos < 1023) buffer[buf_pos++] = hex[k];
                }
            } else if (*p == 'c') {
                char c = (char)va_arg(ap, int);
                if (buf_pos < 1023) buffer[buf_pos++] = c;
            } else if (*p == 's') {
                char* s = va_arg(ap, char*);
                while (*s) {
                    if (buf_pos < 1023) buffer[buf_pos++] = *s++;
                }
            } else if (*p == '%') {
                if (buf_pos < 1023) buffer[buf_pos++] = '%';
            }
        } else {
            if (buf_pos < 1023) buffer[buf_pos++] = *p;
        }
    }
    
    va_end(ap);
    
    if (buf_pos > 0) {
        sys_write(buffer, buf_pos);
    }
    
    return buf_pos;
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