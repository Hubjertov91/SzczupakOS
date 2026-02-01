#include <string.h>

long strlen(const char* s) {
    long i = 0;
    while (s[i]) i++;
    return i;
}

char* strcpy(char* dst, const char* src) {
    long i = 0;
    while (src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return dst;
}

char* strncpy(char* dst, const char* src, long n) {
    long i = 0;
    while (i < n && src[i]) {
        dst[i] = src[i];
        i++;
    }
    while (i < n) {
        dst[i] = '\0';
        i++;
    }
    return dst;
}

int strcmp(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char* a, const char* b, long n) {
    while (n-- > 0 && *a && *b) {
        if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
        a++;
        b++;
    }
    if (n < 0) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char* strcat(char* dst, const char* src) {
    char* p = dst;
    while (*p) p++;
    while (*src) {
        *p++ = *src++;
    }
    *p = '\0';
    return dst;
}

const char* strchr(const char* s, char c) {
    while (*s) {
        if (*s == c) return s;
        s++;
    }
    return (c == '\0') ? s : 0;
}

static char* strtok_ptr = 0;

char* strtok(char* str, const char* delims) {
    if (str) strtok_ptr = str;
    if (!strtok_ptr) return 0;

    while (*strtok_ptr && strchr(delims, *strtok_ptr)) strtok_ptr++;
    if (!*strtok_ptr) return 0;

    char* token = strtok_ptr;
    while (*strtok_ptr && !strchr(delims, *strtok_ptr)) strtok_ptr++;
    if (*strtok_ptr) *strtok_ptr++ = '\0';

    return token;
}

void* memcpy(void* dst, const void* src, long n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (long i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

void* memset(void* dst, int val, long n) {
    unsigned char* d = (unsigned char*)dst;
    for (long i = 0; i < n; i++) d[i] = (unsigned char)val;
    return dst;
}

int memcmp(const void* a, const void* b, long n) {
    const unsigned char* pa = (const unsigned char*)a;
    const unsigned char* pb = (const unsigned char*)b;
    for (long i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return pa[i] - pb[i];
    }
    return 0;
}