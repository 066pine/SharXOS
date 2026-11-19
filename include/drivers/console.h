// SharkXOS - console (VGA text mode + COM1 mirror)
#pragma once
#include <kernel/common.h>

// VGA text colours
enum VgaColor {
    VGA_BLACK = 0, VGA_BLUE, VGA_GREEN, VGA_CYAN,
    VGA_RED, VGA_MAGENTA, VGA_BROWN, VGA_LIGHT_GREY,
    VGA_DARK_GREY, VGA_LIGHT_BLUE, VGA_LIGHT_GREEN, VGA_LIGHT_CYAN,
    VGA_LIGHT_RED, VGA_LIGHT_MAGENTA, VGA_YELLOW, VGA_WHITE
};

namespace console {
void init();
void clear();
void putc(char c);
void write(const char* s);
void write(const char* s, size_t n);
void set_color(VgaColor fg, VgaColor bg = VGA_BLACK);
void reset_color();
VgaColor fg();
VgaColor bg();
void newline();
void backspace();
u16* vga_buffer();
u32  vga_width();
u32  vga_height();
u32  vga_row();
u32  vga_col();
// Raw cell read/write used by diagnostics
u16  read_cell(u32 x, u32 y);
void write_cell(u32 x, u32 y, u16 value);
} // namespace console

// ---- formatted output -----------------------------------------------------
int kvsnprintf(char* buf, size_t size, const char* fmt, va_list ap);
int ksnprintf(char* buf, size_t size, const char* fmt, ...);
void kprintf(const char* fmt, ...);
void klog(const char* tag, const char* fmt, ...);

// Serial-only logging: goes to COM1 (and QEMU debug port), never to VGA.
// Use for periodic background chatter that would disturb the front console.
void kserial_printf(const char* fmt, ...);
void klog_serial(const char* tag, const char* fmt, ...);

namespace console {
void set_mirror_serial(bool enabled);
bool mirror_serial();
} // namespace console
