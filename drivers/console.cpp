// SharkXOS - unified console: VGA text mode + serial mirror + formatting
#include <drivers/console.h>
#include <drivers/vga.h>
#include <drivers/serial.h>
#include <kernel/string.h>
#include <lib/kmath.h>

namespace {

bool g_mirror = true;

void emit(char c) {
    vga::putc(c);
    if (g_mirror) {
        if (c == '\n') serial::putc('\r');
        serial::putc(c);
    }
    kstd::outb(0xE9, (u8)c);   // QEMU debug console
}

} // namespace

namespace console {

void init() {
    vga::init();
    serial::init();
    g_mirror = true;
}

void clear() { vga::clear(); }
void putc(char c) { emit(c); }
void write(const char* s) { while (s && *s) emit(*s++); }
void write(const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) emit(s[i]);
}
void set_color(VgaColor f, VgaColor b) { vga::set_color(f, b); }
void reset_color() { vga::set_color(VGA_LIGHT_GREY, VGA_BLACK); }
VgaColor fg() { return VGA_LIGHT_GREY; }
VgaColor bg() { return VGA_BLACK; }
void newline() { emit('\n'); }
void backspace() { vga::putc('\b'); if (g_mirror) serial::write("\b \b"); }

u16* vga_buffer() { return (u16*)0xB8000; }
u32 vga_width() { return 80; }
u32 vga_height() { return 25; }
u32 vga_row() { return vga::cursor_row; }
u32 vga_col() { return vga::cursor_col; }

u16 read_cell(u32 x, u32 y) {
    if (x >= 80 || y >= 25) return 0;
    return ((u16*)0xB8000)[y * 80 + x];
}

void write_cell(u32 x, u32 y, u16 value) {
    if (x >= 80 || y >= 25) return;
    ((u16*)0xB8000)[y * 80 + x] = value;
}

void set_mirror_serial(bool enabled) { g_mirror = enabled; }
bool mirror_serial() { return g_mirror; }

} // namespace console

// --------------------------------------------------------------- formatting
static void out_char(char* buf, size_t size, int& pos, char c) {
    if ((size_t)pos + 1 < size) buf[pos] = c;
    pos++;
}

static void out_str(char* buf, size_t size, int& pos, const char* s) {
    while (*s) out_char(buf, size, pos, *s++);
}

static void out_pad(char* buf, size_t size, int& pos, char c, int count) {
    for (int i = 0; i < count; i++) out_char(buf, size, pos, c);
}

static bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Emit an already rendered field honouring width / left-align / zero-padding.
static void emit_field(char* buf, size_t size, int& pos, const char* text, int len,
                       int width, bool left, bool zero) {
    int pad = width - len;
    if (pad < 0) pad = 0;

    bool negative = (len > 0 && (text[0] == '-' || text[0] == '+'));

    if (!left && !zero) {
        out_pad(buf, size, pos, ' ', pad);
        out_str(buf, size, pos, text);
        return;
    }
    if (!left && zero) {
        if (negative) {
            out_char(buf, size, pos, text[0]);
            out_pad(buf, size, pos, '0', pad);
            out_str(buf, size, pos, text + 1);
        } else {
            out_pad(buf, size, pos, '0', pad);
            out_str(buf, size, pos, text);
        }
        return;
    }
    out_str(buf, size, pos, text);
    out_pad(buf, size, pos, ' ', pad);
}

int kvsnprintf(char* buf, size_t size, const char* fmt, va_list ap) {
    int pos = 0;
    if (size == 0) return 0;

    while (*fmt) {
        if (*fmt != '%') {
            out_char(buf, size, pos, *fmt++);
            continue;
        }
        fmt++;
        if (*fmt == '%') { out_char(buf, size, pos, '%'); fmt++; continue; }

        bool left = false, zero = false, plus = false;
        for (;;) {
            if (*fmt == '-') { left = true; fmt++; }
            else if (*fmt == '0') { zero = true; fmt++; }
            else if (*fmt == '+') { plus = true; fmt++; }
            else if (*fmt == ' ') { fmt++; }
            else break;
        }

        int width = 0;
        while (is_digit(*fmt)) { width = width * 10 + (*fmt - '0'); fmt++; }

        int precision = -1;
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            while (is_digit(*fmt)) { precision = precision * 10 + (*fmt - '0'); fmt++; }
        }

        int longs = 0;
        while (*fmt == 'l') { longs++; fmt++; }
        if (*fmt == 'z') { longs = 1; fmt++; }

        char spec = *fmt ? *fmt++ : 0;
        char tmp[80];
        int n = 0;

        switch (spec) {
        case 'd':
        case 'i': {
            long long v;
            if (longs >= 2) v = va_arg(ap, long long);
            else if (longs == 1) v = (long long)va_arg(ap, long);
            else v = (long long)va_arg(ap, int);

            bool neg = v < 0;
            unsigned long long uv = neg ? (unsigned long long)(-v) : (unsigned long long)v;
            char digits[24];
            int dn = 0;
            if (uv == 0) digits[dn++] = '0';
            while (uv > 0) { digits[dn++] = (char)('0' + (int)(uv % 10ull)); uv /= 10ull; }
            if (neg) tmp[n++] = '-';
            else if (plus) tmp[n++] = '+';
            while (dn > 0) tmp[n++] = digits[--dn];
            tmp[n] = 0;
            emit_field(buf, size, pos, tmp, n, width, left, zero);
            break;
        }
        case 'u': {
            unsigned long long uv;
            if (longs >= 2) uv = va_arg(ap, unsigned long long);
            else if (longs == 1) uv = (unsigned long long)va_arg(ap, unsigned long);
            else uv = (unsigned long long)va_arg(ap, unsigned int);
            char digits[24];
            int dn = 0;
            if (uv == 0) digits[dn++] = '0';
            while (uv > 0) { digits[dn++] = (char)('0' + (int)(uv % 10ull)); uv /= 10ull; }
            while (dn > 0) tmp[n++] = digits[--dn];
            tmp[n] = 0;
            emit_field(buf, size, pos, tmp, n, width, left, zero);
            break;
        }
        case 'x':
        case 'X': {
            unsigned int v = va_arg(ap, unsigned int);
            const char* digits = (spec == 'x') ? "0123456789abcdef" : "0123456789ABCDEF";
            char num[16];
            int dn = 0;
            if (v == 0) num[dn++] = '0';
            while (v > 0) { num[dn++] = digits[v & 0xF]; v >>= 4; }
            while (dn > 0) tmp[n++] = num[--dn];
            tmp[n] = 0;
            emit_field(buf, size, pos, tmp, n, width, left, zero);
            break;
        }
        case 'p': {
            unsigned int v = (unsigned int)(size_t)va_arg(ap, void*);
            char num[16];
            int dn = 0;
            if (v == 0) num[dn++] = '0';
            while (v > 0) { num[dn++] = "0123456789abcdef"[v & 0xF]; v >>= 4; }
            tmp[n++] = '0';
            tmp[n++] = 'x';
            while (dn > 0) tmp[n++] = num[--dn];
            tmp[n] = 0;
            emit_field(buf, size, pos, tmp, n, width, left, false);
            break;
        }
        case 'c': {
            tmp[n++] = (char)va_arg(ap, int);
            tmp[n] = 0;
            emit_field(buf, size, pos, tmp, n, width, left, false);
            break;
        }
        case 's': {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int len = 0;
            while (s[len] && (precision < 0 || len < precision)) len++;
            char copy[128];
            int cn = len < (int)sizeof(copy) - 1 ? len : (int)sizeof(copy) - 1;
            for (int i = 0; i < cn; i++) copy[i] = s[i];
            copy[cn] = 0;
            emit_field(buf, size, pos, copy, cn, width, left, false);
            break;
        }
        case 'f': {
            double d = va_arg(ap, double);
            int decimals = (precision >= 0) ? precision : 6;
            kmath::format_fixed(d, tmp, sizeof(tmp), decimals);
            emit_field(buf, size, pos, tmp, (int)strlen(tmp), width, left, zero);
            break;
        }
        case 0:
            break;
        default:
            tmp[0] = '%';
            tmp[1] = spec;
            tmp[2] = 0;
            out_str(buf, size, pos, tmp);
            break;
        }
    }

    if (pos >= (int)size) buf[size - 1] = 0;
    else buf[pos] = 0;
    return pos;
}

int ksnprintf(char* buf, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

void kprintf(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    console::write(buf);
}

void klog(const char* tag, const char* fmt, ...) {
    char buf[384];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    console::set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("[ %s ] ", tag);
    console::set_color(VGA_LIGHT_GREY, VGA_BLACK);
    kprintf("%s\n", buf);
}
