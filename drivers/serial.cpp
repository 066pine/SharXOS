// SharkXOS - 16550 UART driver (COM1)
#include <drivers/serial.h>

namespace serial {

static u16 g_port = 0x3F8;
static bool g_ready = false;

void init(u16 port) {
    g_port = port;
    kstd::outb(g_port + 1, 0x00);   // disable interrupts
    kstd::outb(g_port + 3, 0x80);   // enable DLAB
    kstd::outb(g_port + 0, 0x01);   // divisor low: 115200 baud
    kstd::outb(g_port + 1, 0x00);   // divisor high
    kstd::outb(g_port + 3, 0x03);   // 8 bits, no parity, one stop bit
    kstd::outb(g_port + 2, 0xC7);   // enable FIFO, clear, 14 byte threshold
    kstd::outb(g_port + 4, 0x0B);   // IRQs enabled, RTS/DSR set
    g_ready = true;
}

static bool tx_ready() {
    for (u32 i = 0; i < 100000; i++) {
        if (kstd::inb(g_port + 5) & 0x20) return true;
    }
    return false;
}

void putc(char c) {
    if (!g_ready) return;
    if (!tx_ready()) return;
    kstd::outb(g_port, (u8)c);
}

void write(const char* s) {
    while (s && *s) putc(*s++);
}

bool rx_ready() {
    if (!g_ready) return false;
    return (kstd::inb(g_port + 5) & 0x01) != 0;
}

char read() { return (char)kstd::inb(g_port); }

void enable_rx_irq() {
    kstd::outb(g_port + 1, 0x01);   // received data available interrupt
}

u16 port() { return g_port; }

} // namespace serial
