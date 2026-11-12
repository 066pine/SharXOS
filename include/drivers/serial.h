// SharkXOS - 16550 UART serial driver (COM1)
#pragma once
#include <kernel/common.h>

namespace serial {
void init(u16 port = 0x3F8);
void putc(char c);
void write(const char* s);
bool rx_ready();
char read();
void enable_rx_irq();
u16  port();
} // namespace serial
