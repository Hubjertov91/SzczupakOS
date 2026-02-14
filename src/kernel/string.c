#include <kernel/string.h>

void* memset(void* s, int c, size_t n) {
    uint8_t* p = (uint8_t*)s;
    uint8_t val = (uint8_t)c;
    
    while (((uint64_t)p & 7) && n) {
        *p++ = val;
        n--;
    }
    
    if (n >= 8) {
        uint64_t val64 = val;
        val64 |= val64 << 8;
        val64 |= val64 << 16;
        val64 |= val64 << 32;
        
        uint64_t* p64 = (uint64_t*)p;
        while (n >= 8) {
            *p64++ = val64;
            n -= 8;
        }
        p = (uint8_t*)p64;
    }
    
    while (n--) {
        *p++ = val;
    }
    
    return s;
}

void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    
    while (((uint64_t)d & 7) && ((uint64_t)s & 7) && n) {
        *d++ = *s++;
        n--;
    }
    
    if (n >= 8 && !(((uint64_t)d & 7) || ((uint64_t)s & 7))) {
        uint64_t* d64 = (uint64_t*)d;
        const uint64_t* s64 = (const uint64_t*)s;
        
        while (n >= 64) {
            __builtin_prefetch(s64 + 8, 0, 0);
            d64[0] = s64[0];
            d64[1] = s64[1];
            d64[2] = s64[2];
            d64[3] = s64[3];
            d64[4] = s64[4];
            d64[5] = s64[5];
            d64[6] = s64[6];
            d64[7] = s64[7];
            d64 += 8;
            s64 += 8;
            n -= 64;
        }
        
        while (n >= 8) {
            *d64++ = *s64++;
            n -= 8;
        }
        
        d = (uint8_t*)d64;
        s = (const uint8_t*)s64;
    }
    
    while (n--) {
        *d++ = *s++;
    }
    
    return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    
    if (d < s) {
        return memcpy(dest, src, n);
    } else if (d > s) {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    
    return 0;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const uint8_t*)s1 - *(const uint8_t*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const uint8_t*)s1 - *(const uint8_t*)s2;
}