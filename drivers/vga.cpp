// SharkXOS - VGA text mode driver (80x25)
#include <drivers/vga.h>
#include <kernel/string.h>

namespace vga {

static u16* const BUFFER = (u16*)0xB8000;
static const u32 WIDTH = 80;
static const u32 HEIGHT = 25;

u32 cursor_row = 0;
u32 cursor_col = 0;

static u8 g_color = 0x07;   // light grey on black

static inline u16 make_cell(char c, u8 color) {
    return (u16)((u8)c) | ((u16)color << 8);
}

void enable_cursor(u8 start, u8 end) {
    kstd::outb(0x3D4, 0x0A);
    kstd::outb(0x3D5, (kstd::inb(0x3D5) & 0xC0) | start);
    kstd::outb(0x3D4, 0x0B);
    kstd::outb(0x3D5, (kstd::inb(0x3D5) & 0xE0) | end);
}

void update_cursor() {
    u16 pos = (u16)(cursor_row * WIDTH + cursor_col);
    kstd::outb(0x3D4, 0x0F);
    kstd::outb(0x3D5, (u8)(pos & 0xFF));
    kstd::outb(0x3D4, 0x0E);
    kstd::outb(0x3D5, (u8)((pos >> 8) & 0xFF));
}

void init() {
    cursor_row = 0;
    cursor_col = 0;
    g_color = 0x07;
    for (u32 i = 0; i < WIDTH * HEIGHT; i++) BUFFER[i] = make_cell(' ', g_color);
    enable_cursor(14, 15);
    update_cursor();
}

void clear() {
    for (u32 i = 0; i < WIDTH * HEIGHT; i++) BUFFER[i] = make_cell(' ', g_color);
    cursor_row = 0;
    cursor_col = 0;
    update_cursor();
}

void set_color(VgaColor f, VgaColor b) {
    g_color = (u8)(((u8)b << 4) | ((u8)f & 0x0F));
}

void scroll() {
    for (u32 y = 1; y < HEIGHT; y++) {
        for (u32 x = 0; x < WIDTH; x++) {
            BUFFER[(y - 1) * WIDTH + x] = BUFFER[y * WIDTH + x];
        }
    }
    for (u32 x = 0; x < WIDTH; x++) {
        BUFFER[(HEIGHT - 1) * WIDTH + x] = make_cell(' ', g_color);
    }
    cursor_row = HEIGHT - 1;
}

void putc(char c) {
    if (c == '\n') {
        cursor_col = 0;
        cursor_row++;
    } else if (c == '\r') {
        cursor_col = 0;
    } else if (c == '\b') {
        if (cursor_col > 0) {
            cursor_col--;
        } else if (cursor_row > 0) {
            cursor_row--;
            cursor_col = WIDTH - 1;
        }
        BUFFER[cursor_row * WIDTH + cursor_col] = make_cell(' ', g_color);
    } else if (c == '\t') {
        u32 next = (cursor_col + 4) & ~3u;
        while (cursor_col < next && cursor_col < WIDTH) {
            BUFFER[cursor_row * WIDTH + cursor_col] = make_cell(' ', g_color);
            cursor_col++;
        }
        if (cursor_col >= WIDTH) { cursor_col = 0; cursor_row++; }
    } else {
        BUFFER[cursor_row * WIDTH + cursor_col] = make_cell(c, g_color);
        cursor_col++;
        if (cursor_col >= WIDTH) { cursor_col = 0; cursor_row++; }
    }

    if (cursor_row >= HEIGHT) scroll();
    update_cursor();
}

u16 cell(u32 x, u32 y) {
    if (x >= WIDTH || y >= HEIGHT) return 0;
    return BUFFER[y * WIDTH + x];
}

void set_cell(u32 x, u32 y, u16 v) {
    if (x >= WIDTH || y >= HEIGHT) return;
    BUFFER[y * WIDTH + x] = v;
}

} // namespace vga
