// SharkXOS - PS/2 keyboard driver + unified character input queue
#include <drivers/keyboard.h>
#include <kernel/string.h>

namespace keyboard {

static const char kLower[128] = {
    0,   27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t','q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\','z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0,   '*', 0,   ' '
};

static const char kUpper[128] = {
    0,   27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t','Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0,   '*', 0,   ' '
};

#define INPUT_QUEUE_SIZE 512

static volatile char queue[INPUT_QUEUE_SIZE];
static volatile u32 q_head = 0;
static volatile u32 q_tail = 0;

static bool g_shift = false;
static bool g_caps = false;
static bool g_ctrl = false;
static bool g_extended = false;

static void push(char c) {
    u32 next = (q_head + 1) % INPUT_QUEUE_SIZE;
    if (next == q_tail) return;   // overflow: drop
    queue[q_head] = c;
    q_head = next;
}

void irq_handler() {
    u8 sc = kstd::inb(0x60);

    if (sc == 0xE0) { g_extended = true; return; }
    if (g_extended) {
        g_extended = false;
        if (sc & 0x80) return;               // extended key release
        switch (sc) {
        case 0x48: push((char)KEY_UP);     return;
        case 0x50: push((char)KEY_DOWN);   return;
        case 0x4B: push((char)KEY_LEFT);   return;
        case 0x4D: push((char)KEY_RIGHT);  return;
        case 0x47: push((char)KEY_HOME);   return;
        case 0x4F: push((char)KEY_END);    return;
        case 0x53: push((char)KEY_DELETE); return;
        case 0x49: push((char)KEY_PGUP);   return;
        case 0x51: push((char)KEY_PGDN);   return;
        case 0x1C: push('\n');             return;  // keypad Enter
        case 0x35: push('/');              return;  // keypad divide
        default: return;
        }
    }

    bool released = (sc & 0x80) != 0;
    u8 code = (u8)(sc & 0x7F);

    if (code == 0x2A || code == 0x36) { g_shift = !released; return; }
    if (code == 0x1D) { g_ctrl = !released; return; }
    if (code == 0x3A && !released) { g_caps = !g_caps; return; }
    if (released) return;

    if (code == 0x1C) { push('\n'); return; }
    if (code == 0x0E) { push('\b'); return; }
    if (code == 0x0F) { push('\t'); return; }
    if (code == 0x01) { push(27); return; }

    char base = kLower[code];
    if (!base) return;

    bool upper = g_shift;
    if (g_caps && base >= 'a' && base <= 'z') upper = !upper;
    char c = upper ? kUpper[code] : base;
    if (!c) return;

    if (g_ctrl) {
        char lc = kstd::to_lower(c);
        if (lc >= 'a' && lc <= 'z') {
            push((char)(lc - 'a' + 1));   // control characters
            return;
        }
    }
    push(c);
}

void push_serial_char(char c) {
    if (c == '\r') c = '\n';
    if (c == 0x7F) c = '\b';
    push(c);
}

bool try_getchar(char* out) {
    if (q_tail == q_head) return false;
    *out = queue[q_tail];
    q_tail = (q_tail + 1) % INPUT_QUEUE_SIZE;
    return true;
}

void unget_char(char c) {
    u32 flags = kstd::irq_save();
    u32 prev = (q_tail + INPUT_QUEUE_SIZE - 1) % INPUT_QUEUE_SIZE;
    if (prev != q_head) {          // there is room in front of the queue
        q_tail = prev;
        queue[q_tail] = c;
    }
    kstd::irq_restore(flags);
}

char getchar() {
    for (;;) {
        u32 flags = kstd::irq_save();
        char c;
        if (try_getchar(&c)) {
            kstd::irq_restore(flags);
            return c;
        }
        kstd::irq_restore(flags);
        // wait for the next interrupt (timer, keyboard or serial)
        __asm__ volatile("sti; hlt" ::: "memory");
    }
}

void init() {
    q_head = q_tail = 0;
    g_shift = g_caps = g_ctrl = g_extended = false;
    // flush the controller output buffer
    for (int i = 0; i < 16 && (kstd::inb(0x64) & 0x01); i++) kstd::inb(0x60);
}

bool shift_down() { return g_shift; }

} // namespace keyboard
