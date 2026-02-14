#ifndef STDIO_H
#define STDIO_H

#include "stdint.h"
#include "stddef.h"

typedef struct { int dummy; } FILE;

extern FILE* stdin;
extern FILE* stdout;

int printf(const char* fmt, ...);
int putchar(char c);
char* fgets(char* buf, int n, FILE* f);

#endif
