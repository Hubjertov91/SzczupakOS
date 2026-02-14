#ifndef STRING_H
#define STRING_H

#include "stdint.h"

long strlen(const char* s);
char* strcpy(char* dst, const char* src);
char* strncpy(char* dst, const char* src, long n);
int strcmp(const char* a, const char* b);
int strncmp(const char* a, const char* b, long n);
char* strcat(char* dst, const char* src);
const char* strchr(const char* s, char c);
char* strtok(char* str, const char* delims);

void* memcpy(void* dst, const void* src, long n);
void* memset(void* dst, int val, long n);
int memcmp(const void* a, const void* b, long n);

#endif