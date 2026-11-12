// SharkXOS - shared terminal UI helpers
#include <apps/appui.h>
#include <drivers/console.h>
#include <drivers/keyboard.h>

namespace appui {

int read_line(const char* prompt, char* buf, int max) {
    if (prompt) {
        console::set_color(VGA_LIGHT_GREEN, VGA_BLACK);
        console::write(prompt);
        console::reset_color();
    }
    int len = 0;
    for (;;) {
        char c = keyboard::getchar();
        if (c == '\n') { console::putc('\n'); break; }
        if (c == '\b' || c == 127) {
            if (len > 0) { len--; console::backspace(); }
            continue;
        }
        if (c == 3) { console::write("^C\n"); return -1; }
        if (c < 32) continue;
        if (len < max - 1) { buf[len++] = c; console::putc(c); }
    }
    buf[len] = 0;
    return len;
}

void clear() { console::clear(); }

void title(const char* text) {
    console::set_color(VGA_BLACK, VGA_LIGHT_CYAN);
    kprintf(" %-78s", text);
    console::reset_color();
    console::newline();
}

void status(const char* text, bool ok) {
    console::set_color(ok ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    kprintf("%s\n", text);
    console::reset_color();
}

} // namespace appui
