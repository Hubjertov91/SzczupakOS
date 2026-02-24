#ifndef STDLIB_H
#define STDLIB_H

#include "stdint.h"
#include "stddef.h"

long atoi(const char* s);
unsigned long atoul(const char* s);
int itoa(long val, char* buf);
int uitoa(unsigned long val, char* buf);

long abs(long val);
long max(long a, long b);
long min(long a, long b);

void srand(unsigned long seed);
unsigned long rand(void);

void* malloc(size_t size);
void free(void* ptr);
void* calloc(size_t nmemb, size_t size);

#endif