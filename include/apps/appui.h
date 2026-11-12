// SharkXOS - shared terminal UI helpers for the built-in applications
#pragma once
#include <kernel/common.h>

namespace appui {
// Reads one line with echo/backspace handling. Returns the length.
// Returns -1 if the user pressed Ctrl-C.
int read_line(const char* prompt, char* buf, int max);
void clear();
void title(const char* text);
void status(const char* text, bool ok);
} // namespace appui
