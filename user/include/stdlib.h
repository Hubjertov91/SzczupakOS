#ifndef STDLIB_H
#define STDLIB_H

#include "stdint.h"

long atoi(const char* s);
unsigned long atoul(const char* s);
int itoa(long val, char* buf);
int uitoa(unsigned long val, char* buf);

long abs(long val);
long max(long a, long b);
long min(long a, long b);

void srand(unsigned long seed);
unsigned long rand(void);

#endif