// SharkXOS - freestanding string / memory primitives
#pragma once
#include <kernel/common.h>

extern "C" {
size_t strlen(const char* s);
int    strcmp(const char* a, const char* b);
int    strncmp(const char* a, const char* b, size_t n);
char*  strcpy(char* d, const char* s);
char*  strncpy(char* d, const char* s, size_t n);
char*  strcat(char* d, const char* s);
char*  strchr(const char* s, int c);
char*  strrchr(const char* s, int c);
void*  memset(void* d, int c, size_t n);
void*  memcpy(void* d, const void* s, size_t n);
void*  memmove(void* d, const void* s, size_t n);
int    memcmp(const void* a, const void* b, size_t n);
}

namespace kstd {
// string helpers with kernel-friendly semantics
bool  streq(const char* a, const char* b);
bool  starts_with(const char* s, const char* prefix);
int   to_int(const char* s, bool* ok = nullptr);
double to_double(const char* s, bool* ok = nullptr);
void  itoa_u32(u32 v, char* out, int base = 10);
int   atoi_dec(const char* s);
char  to_lower(char c);
char  to_upper(char c);
void  trim(char* s);
}
