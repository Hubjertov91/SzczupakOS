#include <stdlib.h>
#include <stddef.h>

long atoi(const char* s) {
    long result = 0;
    int sign = 1;
    int i = 0;

    while (s[i] == ' ') i++;
    if (s[i] == '-') { sign = -1; i++; }
    else if (s[i] == '+') { i++; }

    while (s[i] >= '0' && s[i] <= '9') {
        result = result * 10 + (s[i] - '0');
        i++;
    }
    return result * sign;
}

unsigned long atoul(const char* s) {
    unsigned long result = 0;
    int i = 0;
    while (s[i] == ' ') i++;
    while (s[i] >= '0' && s[i] <= '9') {
        result = result * 10 + (s[i] - '0');
        i++;
    }
    return result;
}

int itoa(long val, char* buf) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    int sign = 0;
    unsigned long uval;
    if (val < 0) {
        sign = 1;
        uval = (unsigned long)val;
        uval = -uval;
    } else {
        uval = (unsigned long)val;
    }
    char tmp[32];
    int i = 0;
    while (uval > 0) {
        tmp[i++] = '0' + (uval % 10);
        uval /= 10;
    }
    int pos = 0;
    if (sign) buf[pos++] = '-';
    while (i--) buf[pos++] = tmp[i];
    buf[pos] = '\0';
    return pos;
}

int uitoa(unsigned long val, char* buf) {
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    char tmp[32];
    int i = 0;
    while (val > 0) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    int pos = 0;
    while (i--) buf[pos++] = tmp[i];
    buf[pos] = '\0';
    return pos;
}

long abs(long val) {
    if (val < 0) {
        unsigned long u = (unsigned long)val;
        return (long)(-u);
    }
    return val;
}

long max(long a, long b) {
    return a > b ? a : b;
}

long min(long a, long b) {
    return a < b ? a : b;
}

static unsigned long rand_state = 12345;

void srand(unsigned long seed) {
    rand_state = seed;
}

unsigned long rand(void) {
    rand_state ^= rand_state << 13;
    rand_state ^= rand_state >> 7;
    rand_state ^= rand_state << 17;
    return rand_state;
}

static char* heap_start = (char*)0x10000000;
static char* heap_end = (char*)0x10000000;
static const size_t HEAP_SIZE = 0x100000; // 1MB

void* malloc(size_t size) {
    if (size == 0) return NULL;
    
    size = (size + 7) & ~7; // 8-byte alignment
    
    if (heap_end + size > heap_start + HEAP_SIZE) {
        return NULL; // Out of memory
    }
    
    void* ptr = heap_end;
    heap_end += size;
    return ptr;
}

void free(void* ptr) {
    // Simple implementation - doesn't actually free memory
    // In a real OS this would be more sophisticated
    (void)ptr;
}