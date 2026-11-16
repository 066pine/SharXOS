// SharkXOS - keyboard driver
#pragma once
#include <kernel/common.h>

namespace keyboard {

// Non-ASCII keys are delivered as bytes in the 0x90..0x9f range so they cannot
// be confused with the printable characters produced by the normal keymap.
// Serial terminals sending ANSI escape sequences are translated by the caller.
enum SpecialKey {
    KEY_UP     = 0x90,
    KEY_DOWN   = 0x91,
    KEY_LEFT   = 0x92,
    KEY_RIGHT  = 0x93,
    KEY_HOME   = 0x94,
    KEY_END    = 0x95,
    KEY_DELETE = 0x96,
    KEY_PGUP   = 0x97,
    KEY_PGDN   = 0x98,
};

void init();
void irq_handler();          // called from IRQ1
char getchar();              // blocking ASCII read (keyboard + serial)
bool try_getchar(char* out); // non blocking
void unget_char(char c);     // put the last read character back (peek/unget)
bool shift_down();
void push_serial_char(char c);
} // namespace keyboard
