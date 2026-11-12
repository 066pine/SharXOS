// SharkXOS - freestanding string and memory primitives
#include <kernel/string.h>

extern "C" size_t strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

extern "C" int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

extern "C" int strncmp(const char* a, const char* b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

extern "C" char* strcpy(char* d, const char* s) {
    char* r = d;
    while ((*d++ = *s++)) {}
    return r;
}

extern "C" char* strncpy(char* d, const char* s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}

extern "C" char* strcat(char* d, const char* s) {
    char* r = d;
    while (*d) d++;
    while ((*d++ = *s++)) {}
    return r;
}

extern "C" char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == 0) ? (char*)s : nullptr;
}

extern "C" char* strrchr(const char* s, int c) {
    const char* last = nullptr;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    return (c == 0) ? (char*)s : (char*)last;
}

extern "C" void* memset(void* d, int c, size_t n) {
    u8* p = (u8*)d;
    for (size_t i = 0; i < n; i++) p[i] = (u8)c;
    return d;
}

extern "C" void* memcpy(void* d, const void* s, size_t n) {
    u8* dp = (u8*)d;
    const u8* sp = (const u8*)s;
    for (size_t i = 0; i < n; i++) dp[i] = sp[i];
    return d;
}

extern "C" void* memmove(void* d, const void* s, size_t n) {
    u8* dp = (u8*)d;
    const u8* sp = (const u8*)s;
    if (dp == sp || n == 0) return d;
    if (dp < sp) {
        for (size_t i = 0; i < n; i++) dp[i] = sp[i];
    } else {
        for (size_t i = n; i > 0; i--) dp[i - 1] = sp[i - 1];
    }
    return d;
}

extern "C" int memcmp(const void* a, const void* b, size_t n) {
    const u8* pa = (const u8*)a;
    const u8* pb = (const u8*)b;
    for (size_t i = 0; i < n; i++) {
        if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
    }
    return 0;
}

namespace kstd {

bool streq(const char* a, const char* b) { return strcmp(a, b) == 0; }

bool starts_with(const char* s, const char* prefix) {
    while (*prefix) {
        if (*s++ != *prefix++) return false;
    }
    return true;
}

int to_int(const char* s, bool* ok) {
    if (!s) { if (ok) *ok = false; return 0; }
    while (*s == ' ') s++;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    else if (*s == '+') s++;
    if (*s < '0' || *s > '9') { if (ok) *ok = false; return 0; }
    long long v = 0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        if (v > 2147483647LL) { if (ok) *ok = false; return 0; }
        s++;
        digits++;
    }
    while (*s == ' ') s++;
    if (*s != 0 || digits == 0) { if (ok) *ok = false; return 0; }
    if (ok) *ok = true;
    return (int)(neg ? -v : v);
}

double to_double(const char* s, bool* ok) {
    if (!s) { if (ok) *ok = false; return 0.0; }
    while (*s == ' ') s++;
    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    else if (*s == '+') s++;
    double v = 0.0;
    int digits = 0;
    while (*s >= '0' && *s <= '9') { v = v * 10.0 + (double)(*s - '0'); s++; digits++; }
    if (*s == '.') {
        s++;
        double scale = 0.1;
        while (*s >= '0' && *s <= '9') { v += (double)(*s - '0') * scale; scale *= 0.1; s++; digits++; }
    }
    if (digits == 0) { if (ok) *ok = false; return 0.0; }
    if (*s == 'e' || *s == 'E') {
        s++;
        bool eneg = false;
        if (*s == '-') { eneg = true; s++; } else if (*s == '+') s++;
        int e = 0;
        while (*s >= '0' && *s <= '9') { e = e * 10 + (*s - '0'); s++; }
        double p = 1.0;
        for (int i = 0; i < e; i++) p *= 10.0;
        v = eneg ? (v / p) : (v * p);
    }
    while (*s == ' ') s++;
    if (*s != 0) { if (ok) *ok = false; return 0.0; }
    if (ok) *ok = true;
    return neg ? -v : v;
}

void itoa_u32(u32 v, char* out, int base) {
    if (base < 2 || base > 16) base = 10;
    char tmp[36];
    int i = 0;
    if (v == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    while (v > 0 && i < 35) {
        u32 d = v % (u32)base;
        tmp[i++] = (char)(d < 10 ? '0' + d : 'a' + (d - 10));
        v /= (u32)base;
    }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = 0;
}

int atoi_dec(const char* s) { return to_int(s, nullptr); }
char to_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
char to_upper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }

void trim(char* s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) s[--n] = 0;
    size_t start = 0;
    while (s[start] == ' ' || s[start] == '\t') start++;
    if (start) memmove(s, s + start, n - start + 1);
}

} // namespace kstd
