// SharkXOS - VGA text mode driver
#pragma once
#include <kernel/common.h>
#include <drivers/console.h>

namespace vga {
void init();
void clear();
void putc(char c);
void set_color(VgaColor fg, VgaColor bg);
void scroll();
u16  cell(u32 x, u32 y);
void set_cell(u32 x, u32 y, u16 v);
void enable_cursor(u8 start, u8 end);
void update_cursor();
extern u32 cursor_row;
extern u32 cursor_col;
}
