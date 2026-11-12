// SharkXOS - keyboard driver
#pragma once
#include <kernel/common.h>

namespace keyboard {
void init();
void irq_handler();          // called from IRQ1
char getchar();              // blocking ASCII read (keyboard + serial)
bool try_getchar(char* out); // non blocking
bool shift_down();
void push_serial_char(char c);
} // namespace keyboard
