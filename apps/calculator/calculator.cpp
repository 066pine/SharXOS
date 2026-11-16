// SharkXOS - scientific calculator
#include <apps/apps.h>
#include <lib/kmath.h>
#include <kernel/string.h>
#include <drivers/console.h>
#include <drivers/keyboard.h>
#include <drivers/serial.h>
#include <drivers/vga.h>
#include <kernel/time.h>
#include <memory/heap.h>

namespace {

// ------------------------------------------------------------- expression parser
struct Parser {
    const char* src;
    const char* p;
    bool ok;
    const char* error;
    bool degrees;
    double ans;
};

void fail(Parser& ps, const char* msg) {
    if (ps.ok) { ps.ok = false; ps.error = msg; }
}

void skip_ws(Parser& ps) {
    while (*ps.p == ' ' || *ps.p == '\t') ps.p++;
}

bool eat(Parser& ps, char c) {
    skip_ws(ps);
    if (*ps.p == c) { ps.p++; return true; }
    return false;
}

double parse_expr(Parser& ps);

bool is_ident_start(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
bool is_ident_char(char c) { return is_ident_start(c) || (c >= '0' && c <= '9'); }

void read_ident(Parser& ps, char* out, int max) {
    int n = 0;
    skip_ws(ps);
    while (is_ident_char(*ps.p) && n < max - 1) out[n++] = kstd::to_lower(*ps.p++);
    out[n] = 0;
}

bool match_ident(const char* a, const char* b) { return strcmp(a, b) == 0; }

double call_function(Parser& ps, const char* name) {
    // two argument functions
    if (match_ident(name, "ncr") || match_ident(name, "npr") || match_ident(name, "c") ||
        match_ident(name, "p")) {
        if (!eat(ps, '(')) { fail(ps, "expected '('"); return 0; }
        double a = parse_expr(ps);
        if (!eat(ps, ',')) { fail(ps, "expected ','"); return 0; }
        double b = parse_expr(ps);
        if (!eat(ps, ')')) { fail(ps, "expected ')'"); return 0; }
        bool ok = true;
        if (a < 0 || b < 0 || a != kmath::floor(a) || b != kmath::floor(b)) {
            fail(ps, "arguments must be non-negative integers");
            return 0;
        }
        int n = (int)a, r = (int)b;
        double v = match_ident(name, "ncr") || match_ident(name, "c")
                       ? kmath::ncr(n, r, &ok)
                       : kmath::npr(n, r, &ok);
        if (!ok) fail(ps, "invalid combination/permutation");
        return v;
    }

    if (!eat(ps, '(')) { fail(ps, "expected '(' after function name"); return 0; }
    double x = parse_expr(ps);
    if (!eat(ps, ')')) { fail(ps, "expected ')'"); return 0; }
    if (!ps.ok) return 0;

    bool overflow = false;
    if (match_ident(name, "sin")) return kmath::sin(ps.degrees ? kmath::deg2rad(x) : x);
    if (match_ident(name, "cos")) return kmath::cos(ps.degrees ? kmath::deg2rad(x) : x);
    if (match_ident(name, "tan")) {
        double c = kmath::cos(ps.degrees ? kmath::deg2rad(x) : x);
        if (kmath::fabs(c) < 1e-15) { fail(ps, "tan undefined at this angle"); return 0; }
        return kmath::tan(ps.degrees ? kmath::deg2rad(x) : x);
    }
    if (match_ident(name, "asin")) {
        if (x < -1.0 || x > 1.0) { fail(ps, "asin domain error (|x| <= 1)"); return 0; }
        double r = kmath::asin(x);
        return ps.degrees ? kmath::rad2deg(r) : r;
    }
    if (match_ident(name, "acos")) {
        if (x < -1.0 || x > 1.0) { fail(ps, "acos domain error (|x| <= 1)"); return 0; }
        double r = kmath::acos(x);
        return ps.degrees ? kmath::rad2deg(r) : r;
    }
    if (match_ident(name, "atan")) {
        double r = kmath::atan(x);
        return ps.degrees ? kmath::rad2deg(r) : r;
    }
    if (match_ident(name, "sqrt")) {
        if (x < 0) { fail(ps, "sqrt domain error (negative argument)"); return 0; }
        return kmath::sqrt(x);
    }
    if (match_ident(name, "ln")) {
        if (x <= 0) { fail(ps, "ln domain error (argument must be > 0)"); return 0; }
        return kmath::ln(x);
    }
    if (match_ident(name, "log")) {
        if (x <= 0) { fail(ps, "log domain error (argument must be > 0)"); return 0; }
        return kmath::log10(x);
    }
    if (match_ident(name, "exp")) return kmath::exp(x);
    if (match_ident(name, "abs")) return kmath::fabs(x);
    if (match_ident(name, "floor")) return kmath::floor(x);
    if (match_ident(name, "ceil")) return kmath::ceil(x);
    if (match_ident(name, "round")) return kmath::round(x);
    if (match_ident(name, "fact")) {
        if (x < 0 || x != kmath::floor(x) || x > 170) { fail(ps, "fact expects 0..170"); return 0; }
        return kmath::factorial((int)x, &overflow);
    }

    fail(ps, "unknown function");
    return 0;
}

double parse_number(Parser& ps) {
    skip_ws(ps);
    bool any = false;
    double v = 0;
    while (*ps.p >= '0' && *ps.p <= '9') { v = v * 10.0 + (*ps.p - '0'); ps.p++; any = true; }
    if (*ps.p == '.') {
        ps.p++;
        double s = 0.1;
        while (*ps.p >= '0' && *ps.p <= '9') { v += (*ps.p - '0') * s; s *= 0.1; ps.p++; any = true; }
    }
    if (!any) { fail(ps, "expected a number"); return 0; }
    if (*ps.p == 'e' || *ps.p == 'E') {
        const char* save = ps.p;
        ps.p++;
        bool neg = false;
        if (*ps.p == '-') { neg = true; ps.p++; } else if (*ps.p == '+') ps.p++;
        if (*ps.p >= '0' && *ps.p <= '9') {
            int e = 0;
            while (*ps.p >= '0' && *ps.p <= '9') { e = e * 10 + (*ps.p - '0'); ps.p++; }
            if (e > 308) { fail(ps, "exponent overflow"); return 0; }
            double mul = 1.0;
            for (int i = 0; i < e; i++) mul *= 10.0;
            v = neg ? v / mul : v * mul;
        } else {
            ps.p = save;
        }
    }
    return v;
}

double parse_primary(Parser& ps) {
    skip_ws(ps);

    if (*ps.p == '(') {
        ps.p++;
        double v = parse_expr(ps);
        if (!eat(ps, ')')) { fail(ps, "unbalanced parentheses"); return 0; }
        return v;
    }

    if ((*ps.p >= '0' && *ps.p <= '9') || *ps.p == '.') return parse_number(ps);

    if (is_ident_start(*ps.p)) {
        char name[16];
        read_ident(ps, name, sizeof(name));
        if (match_ident(name, "pi")) return kmath::PI;
        if (match_ident(name, "e")) return kmath::E;
        if (match_ident(name, "ans")) return ps.ans;
        return call_function(ps, name);
    }

    fail(ps, "unexpected character");
    return 0;
}

double parse_postfix(Parser& ps) {
    double v = parse_primary(ps);
    for (;;) {
        skip_ws(ps);
        if (*ps.p != '!') break;
        ps.p++;
        if (v < 0 || v != kmath::floor(v) || v > 170) {
            fail(ps, "factorial needs an integer in 0..170");
            return 0;
        }
        bool ovf = false;
        v = kmath::factorial((int)v, &ovf);
        if (ovf) { fail(ps, "factorial overflow"); return 0; }
    }
    return v;
}

double parse_unary(Parser& ps) {
    skip_ws(ps);
    if (*ps.p == '-') { ps.p++; return -parse_unary(ps); }
    if (*ps.p == '+') { ps.p++; return parse_unary(ps); }
    return parse_postfix(ps);
}

double parse_power(Parser& ps) {
    double base = parse_unary(ps);
    skip_ws(ps);
    if (*ps.p == '^') {
        ps.p++;
        double exp = parse_unary(ps);      // right associative
        if (!ps.ok) return 0;
        double r = kmath::pow(base, exp);
        if (kmath::is_nan(r)) { fail(ps, "invalid power (domain error)"); return 0; }
        if (kmath::is_inf(r)) { fail(ps, "power overflow"); return 0; }
        return r;
    }
    return base;
}

bool starts_atom(Parser& ps) {
    skip_ws(ps);
    char c = *ps.p;
    return (c >= '0' && c <= '9') || c == '.' || c == '(' || is_ident_start(c);
}

double parse_term(Parser& ps) {
    double v = parse_power(ps);
    for (;;) {
        skip_ws(ps);
        if (*ps.p == '*') { ps.p++; v *= parse_power(ps); }
        else if (*ps.p == '/') {
            ps.p++;
            double d = parse_power(ps);
            if (d == 0.0) { fail(ps, "division by zero"); return 0; }
            v /= d;
        } else if (*ps.p == '%') {
            ps.p++;
            double d = parse_power(ps);
            if (d == 0.0) { fail(ps, "modulo by zero"); return 0; }
            v = kmath::fmod(v, d);
        } else if (starts_atom(ps)) {
            v *= parse_power(ps);          // implicit multiplication: 2pi, 3(4+1)
        } else {
            break;
        }
        if (!ps.ok) return 0;
        if (kmath::is_inf(v) || kmath::is_nan(v)) {
            fail(ps, "arithmetic overflow");
            return 0;
        }
    }
    return v;
}

double parse_expr(Parser& ps) {
    double v = parse_term(ps);
    for (;;) {
        skip_ws(ps);
        if (*ps.p == '+') { ps.p++; v += parse_term(ps); }
        else if (*ps.p == '-') { ps.p++; v -= parse_term(ps); }
        else break;
        if (!ps.ok) return 0;
        if (kmath::is_inf(v) || kmath::is_nan(v)) { fail(ps, "arithmetic overflow"); return 0; }
    }
    return v;
}

// -------------------------------------------------------------- application
bool g_degrees = false;
double g_memory = 0;
double g_ans = 0;
char g_render[64];

struct HistoryEntry {
    char expr[64];
    char result[32];
};
HistoryEntry g_history[8];
u32 g_history_count = 0;
u32 g_history_next = 0;

void add_history(const char* expr, const char* result) {
    strncpy(g_history[g_history_next].expr, expr, sizeof(g_history[0].expr) - 1);
    g_history[g_history_next].expr[sizeof(g_history[0].expr) - 1] = 0;
    strncpy(g_history[g_history_next].result, result, sizeof(g_history[0].result) - 1);
    g_history[g_history_next].result[sizeof(g_history[0].result) - 1] = 0;
    g_history_next = (g_history_next + 1) % 8;
    if (g_history_count < 8) g_history_count++;
}

// Evaluates one complete expression using the current DEG/RAD mode and `ans`.
bool eval_expression(const char* text, double* out, const char** error) {
    Parser ps;
    ps.src = text;
    ps.p = text;
    ps.ok = true;
    ps.error = nullptr;
    ps.degrees = g_degrees;
    ps.ans = g_ans;

    double v = parse_expr(ps);
    skip_ws(ps);
    if (ps.ok && *ps.p != 0) fail(ps, "trailing characters");
    if (!ps.ok) {
        if (error) *error = ps.error ? ps.error : "invalid expression";
        return false;
    }
    if (kmath::is_nan(v)) {
        if (error) *error = "result is not a number";
        return false;
    }
    if (kmath::is_inf(v)) {
        if (error) *error = "overflow (result out of range)";
        return false;
    }
    *out = v;
    return true;
}

void print_help() {
    kprintf("calculator commands:\n");
    kprintf("  <expression>   evaluate, e.g. 2+3*4, sin(pi/2), 5!, sqrt(2), 2^10\n");
    kprintf("  deg / rad      switch angle mode (currently %s)\n", g_degrees ? "DEG" : "RAD");
    kprintf("  m+ <expr>      store a value in memory     mr  recall\n");
    kprintf("  mc             clear memory                ans last result\n");
    kprintf("  hist           show recent calculations\n");
    kprintf("  bin|hex|dec <expr>   convert result to base 2/16/10\n");
    kprintf("  ncr(n,r) npr(n,r)    combinations / permutations\n");
    kprintf("  functions: sin cos tan asin acos atan ln log sqrt exp abs floor ceil\n");
    kprintf("             round fact(n)  constants: pi e  operators: + - * / %% ^ !\n");
    kprintf("  ui             open the full-screen arrow-key calculator\n");
    kprintf("  exit / quit    leave the calculator\n");
}

void print_conversion(const char* text, int base) {
    double v;
    const char* error = nullptr;
    if (!eval_expression(text, &v, &error)) {
        console::set_color(VGA_LIGHT_RED, VGA_BLACK);
        kprintf("error: %s\n", error ? error : "invalid expression");
        console::reset_color();
        return;
    }
    if (v != kmath::floor(v) || kmath::fabs(v) > 2147483647.0) {
        kprintf("base conversion needs an integer that fits in 32 bits\n");
        return;
    }
    long long iv = (long long)v;
    u32 uv = (u32)(iv < 0 ? -iv : iv);
    char buf[40];
    kstd::itoa_u32(uv, buf, base);
    const char* prefix = base == 2 ? "0b" : (base == 16 ? "0x" : "");
    kprintf("%s%s%s = %lld decimal\n", iv < 0 ? "-" : "", prefix, buf, iv);
}

int run_line(const char* line) {
    char buf[160];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    kstd::trim(buf);
    if (!buf[0]) return 0;

    if (strcmp(buf, "exit") == 0 || strcmp(buf, "quit") == 0) return 1;
    if (strcmp(buf, "help") == 0 || strcmp(buf, "?") == 0) { print_help(); return 0; }
    if (strcmp(buf, "deg") == 0) { g_degrees = true; kprintf("angle mode: degrees\n"); return 0; }
    if (strcmp(buf, "rad") == 0) { g_degrees = false; kprintf("angle mode: radians\n"); return 0; }
    if (strcmp(buf, "mc") == 0) { g_memory = 0; kprintf("memory cleared\n"); return 0; }
    if (strcmp(buf, "mr") == 0) {
        kmath::format_double(g_memory, g_render, sizeof(g_render));
        kprintf("memory = %s\n", g_render);
        return 0;
    }

    if (kstd::starts_with(buf, "m+")) {
        double v;
        if (!eval_expression(buf + 2, &v, nullptr)) {
            kprintf("usage: m+ <number or expression>\n");
            return 0;
        }
        g_memory += v;
        kmath::format_double(g_memory, g_render, sizeof(g_render));
        kprintf("memory = %s\n", g_render);
        return 0;
    }

    if (strcmp(buf, "hist") == 0) {
        if (g_history_count == 0) { kprintf("history is empty\n"); return 0; }
        for (u32 i = 0; i < g_history_count; i++) {
            u32 idx = (g_history_next + 8 - g_history_count + i) % 8;
            kprintf("  %s = %s\n", g_history[idx].expr, g_history[idx].result);
        }
        return 0;
    }

    if (kstd::starts_with(buf, "bin ")) { print_conversion(buf + 4, 2); return 0; }
    if (kstd::starts_with(buf, "hex ")) { print_conversion(buf + 4, 16); return 0; }
    if (kstd::starts_with(buf, "dec ")) { print_conversion(buf + 4, 10); return 0; }

    double v;
    const char* error = nullptr;
    if (!eval_expression(buf, &v, &error)) {
        console::set_color(VGA_LIGHT_RED, VGA_BLACK);
        kprintf("error: %s\n", error ? error : "invalid expression");
        console::reset_color();
        return 0;
    }

    g_ans = v;
    kmath::format_double(v, g_render, sizeof(g_render));
    console::set_color(VGA_LIGHT_GREEN, VGA_BLACK);
    kprintf("= %s\n", g_render);
    console::reset_color();
    add_history(buf, g_render);
    return 0;
}

int calculator_repl() {
    console::set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    kprintf("SharkXOS scientific calculator - type 'help' for commands, 'exit' to leave\n");
    console::reset_color();

    char line[160];
    for (;;) {
        console::set_color(VGA_YELLOW, VGA_BLACK);
        kprintf("calc [%s]> ", g_degrees ? "deg" : "rad");
        console::reset_color();

        int len = 0;
        for (;;) {
            char c = keyboard::getchar();
            if (c == '\n') { console::putc('\n'); break; }
            if (c == '\b' || c == 127) {
                if (len > 0) { len--; console::backspace(); }
                continue;
            }
            if (c == 3) { len = 0; console::write("^C\n"); break; }
            if (c < 32) continue;
            if (len < (int)sizeof(line) - 1) { line[len++] = c; console::putc(c); }
        }
        line[len] = 0;
        if (run_line(line)) break;
    }
    kprintf("calculator closed\n");
    return 0;
}

// ------------------------------------------------------- arrow-key keypad UI
// The UI is drawn directly into VGA text memory instead of going through
// kprintf: that keeps the cursor in place and lets the whole 80x25 screen be
// laid out like the front panel of a scientific calculator.  The previous
// screen and cursor position are saved on entry and restored on exit.

const int UI_COLS      = 7;
const int UI_ROWS      = 5;
const int UI_COUNT     = UI_COLS * UI_ROWS;
const int UI_BTN_W     = 6;
const int UI_BTN_H     = 2;
const int UI_GRID_X    = 16;
const int UI_GRID_Y    = 8;
const int UI_STEP_X    = 7;
const int UI_STEP_Y    = 3;
const int UI_DISP_X    = 17;
const int UI_DISP_Y    = 2;
const int UI_DISP_W    = 46;
const int UI_DISP_H    = 5;
const int UI_TEXT_W    = UI_DISP_W - 2;
const int UI_START_AT  = 14;   // the "7" key

enum UiAction {
    UI_INSERT, UI_CLEAR, UI_DEL, UI_EQUALS,
    UI_ANS, UI_MPLUS, UI_MR, UI_MC, UI_DEG
};

struct UiButton {
    const char* label;
    const char* token;
    UiAction action;
    VgaColor fg;
    VgaColor bg;
};

const UiButton UI_BUTTONS[UI_COUNT] = {
    // scientific functions
    {"sin",  "sin(",  UI_INSERT, VGA_WHITE, VGA_BLUE},
    {"cos",  "cos(",  UI_INSERT, VGA_WHITE, VGA_BLUE},
    {"tan",  "tan(",  UI_INSERT, VGA_WHITE, VGA_BLUE},
    {"ln",   "ln(",   UI_INSERT, VGA_WHITE, VGA_BLUE},
    {"log",  "log(",  UI_INSERT, VGA_WHITE, VGA_BLUE},
    {"sqrt", "sqrt(", UI_INSERT, VGA_WHITE, VGA_BLUE},
    {"^",    "^",     UI_INSERT, VGA_WHITE, VGA_BLUE},

    // constants, grouping and clearing
    {"(",    "(",     UI_INSERT, VGA_BLACK, VGA_CYAN},
    {")",    ")",     UI_INSERT, VGA_BLACK, VGA_CYAN},
    {"pi",   "pi",    UI_INSERT, VGA_BLACK, VGA_CYAN},
    {"e",    "e",     UI_INSERT, VGA_BLACK, VGA_CYAN},
    {"!",    "!",     UI_INSERT, VGA_WHITE, VGA_BLUE},
    {"%",    "%",     UI_INSERT, VGA_WHITE, VGA_BLUE},
    {"C",    "",      UI_CLEAR,  VGA_WHITE, VGA_RED},

    // numeric pad and memory
    {"7",    "7",     UI_INSERT, VGA_BLACK, VGA_LIGHT_GREY},
    {"8",    "8",     UI_INSERT, VGA_BLACK, VGA_LIGHT_GREY},
    {"9",    "9",     UI_INSERT, VGA_BLACK, VGA_LIGHT_GREY},
    {"/",    "/",     UI_INSERT, VGA_WHITE, VGA_BLUE},
    {"DEL",  "",      UI_DEL,    VGA_WHITE, VGA_RED},
    {"ANS",  "ans",   UI_ANS,    VGA_WHITE, VGA_MAGENTA},
    {"M+",   "",      UI_MPLUS,  VGA_WHITE, VGA_MAGENTA},

    {"4",    "4",     UI_INSERT, VGA_BLACK, VGA_LIGHT_GREY},
    {"5",    "5",     UI_INSERT, VGA_BLACK, VGA_LIGHT_GREY},
    {"6",    "6",     UI_INSERT, VGA_BLACK, VGA_LIGHT_GREY},
    {"*",    "*",     UI_INSERT, VGA_WHITE, VGA_BLUE},
    {"MR",   "",      UI_MR,     VGA_WHITE, VGA_MAGENTA},
    {"DEG",  "",      UI_DEG,    VGA_WHITE, VGA_BROWN},
    {".",    ".",     UI_INSERT, VGA_BLACK, VGA_LIGHT_GREY},

    {"1",    "1",     UI_INSERT, VGA_BLACK, VGA_LIGHT_GREY},
    {"2",    "2",     UI_INSERT, VGA_BLACK, VGA_LIGHT_GREY},
    {"3",    "3",     UI_INSERT, VGA_BLACK, VGA_LIGHT_GREY},
    {"-",    "-",     UI_INSERT, VGA_WHITE, VGA_BLUE},
    {"+",    "+",     UI_INSERT, VGA_WHITE, VGA_BLUE},
    {"0",    "0",     UI_INSERT, VGA_BLACK, VGA_LIGHT_GREY},
    {"=",    "",      UI_EQUALS, VGA_BLACK, VGA_GREEN},
};

char g_expr[96];
char g_result[96];
char g_status[80];
bool g_has_result = false;
bool g_result_ok = true;
bool g_status_ok = true;
bool g_fresh = false;      // last action was "="
bool g_direct = false;     // the user typed since the last focus change
int  g_focus = UI_START_AT;

char g_ui_log[12][96];
u32 g_ui_log_count = 0;

// Results are buffered and printed after the screen is restored.  Writing them
// while the UI is active would overwrite the mirrored serial screen.
void ui_log(const char* text) {
    if (g_ui_log_count >= 12) return;
    strncpy(g_ui_log[g_ui_log_count], text, sizeof(g_ui_log[0]) - 1);
    g_ui_log[g_ui_log_count][sizeof(g_ui_log[0]) - 1] = 0;
    g_ui_log_count++;
}

void ui_status(const char* text, bool ok) {
    strncpy(g_status, text, sizeof(g_status) - 1);
    g_status[sizeof(g_status) - 1] = 0;
    g_status_ok = ok;
}

// The VGA screen is the primary display, but SharkXOS mirrors the console to
// COM1 and runs headless by default.  Keep a shadow copy of the 80x25 screen
// and send only changed rows to the serial terminal, using ANSI cursor
// addressing, so the same UI is usable from a serial CLI without making every
// keypress wait for a full-screen redraw.
char g_mirror[25][81];
bool g_mirror_ready = false;

void ui_serial_goto(int x, int y) {
    char seq[20];
    ksnprintf(seq, sizeof(seq), "\033[%d;%dH", y + 1, x + 1);
    serial::write(seq);
}

void ui_mirror() {
    for (int y = 0; y < 25; y++) {
        char row[81];
        int last = -1;
        for (int x = 0; x < 80; x++) {
            char c = (char)(console::read_cell((u32)x, (u32)y) & 0xFF);
            if (c < 32 || c > 126) c = ' ';
            row[x] = c;
            if (c != ' ') last = x;
        }
        row[80] = 0;
        row[last + 1] = 0;          // -1 means an all-space row

        if (!g_mirror_ready || strcmp(g_mirror[y], row) != 0) {
            ui_serial_goto(0, y);
            serial::write(row);
            serial::write("\033[K");
            strncpy(g_mirror[y], row, 80);
            g_mirror[y][80] = 0;
        }
    }
    g_mirror_ready = true;
}

u16 ui_attr(VgaColor fg, VgaColor bg) {
    return (u16)(((u16)bg << 4) | (u16)fg);
}

void ui_cell(int x, int y, char c, VgaColor fg, VgaColor bg) {
    console::write_cell((u32)x, (u32)y, (u16)(u8)c | (u16)(ui_attr(fg, bg) << 8));
}

void ui_text(int x, int y, const char* s, VgaColor fg, VgaColor bg) {
    if (!s) return;
    for (int i = 0; s[i] && x + i < 80; i++) ui_cell(x + i, y, s[i], fg, bg);
}

void ui_fill(int x, int y, int w, int h, char c, VgaColor fg, VgaColor bg) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) ui_cell(x + i, y + j, c, fg, bg);
}

void ui_box(int x, int y, int w, int h, VgaColor fg, VgaColor bg) {
    for (int i = 1; i < w - 1; i++) {
        ui_cell(x + i, y, '-', fg, bg);
        ui_cell(x + i, y + h - 1, '-', fg, bg);
    }
    for (int j = 1; j < h - 1; j++) {
        ui_cell(x, y + j, '|', fg, bg);
        ui_cell(x + w - 1, y + j, '|', fg, bg);
    }
    ui_cell(x, y, '+', fg, bg);
    ui_cell(x + w - 1, y, '+', fg, bg);
    ui_cell(x, y + h - 1, '+', fg, bg);
    ui_cell(x + w - 1, y + h - 1, '+', fg, bg);
}

void ui_draw_button(int index) {
    int col = index % UI_COLS;
    int row = index / UI_COLS;
    int x = UI_GRID_X + col * UI_STEP_X;
    int y = UI_GRID_Y + row * UI_STEP_Y;
    const UiButton& b = UI_BUTTONS[index];

    bool focused = (index == g_focus);
    VgaColor fg = focused ? VGA_BLACK : b.fg;
    VgaColor bg = focused ? VGA_YELLOW : b.bg;

    ui_fill(x, y, UI_BTN_W, UI_BTN_H, ' ', fg, bg);

    int n = (int)strlen(b.label);
    int total = n + (focused ? 2 : 0);
    if (total > UI_BTN_W) total = UI_BTN_W;
    int sx = x + (UI_BTN_W - total) / 2;

    if (focused) { ui_cell(sx, y, '[', fg, bg); sx++; }
    for (int i = 0; i < n && sx + i < x + UI_BTN_W; i++)
        ui_cell(sx + i, y, b.label[i], fg, bg);
    if (focused && sx + n < x + UI_BTN_W) ui_cell(sx + n, y, ']', fg, bg);
}

void ui_draw() {
    ui_fill(0, 0, 80, 25, ' ', VGA_LIGHT_GREY, VGA_BLACK);

    // title bar
    ui_fill(0, 0, 80, 1, ' ', VGA_BLACK, VGA_CYAN);
    ui_text(2, 0, "SharkXOS Scientific Calculator", VGA_BLACK, VGA_CYAN);
    ui_text(80 - 10, 0, "ESC exit", VGA_BLACK, VGA_CYAN);

    // outer case and display
    ui_box(15, 1, 50, 23, VGA_CYAN, VGA_BLACK);
    ui_box(UI_DISP_X, UI_DISP_Y, UI_DISP_W, UI_DISP_H, VGA_DARK_GREY, VGA_BLACK);

    // status indicators
    ui_text(18, 3, g_degrees ? "DEG" : "RAD", VGA_YELLOW, VGA_BLACK);
    if (g_memory != 0.0) {
        char mbuf[40];
        kmath::format_double(g_memory, mbuf, sizeof(mbuf));
        ui_text(23, 3, "M", VGA_LIGHT_MAGENTA, VGA_BLACK);
        ui_text(25, 3, mbuf, VGA_LIGHT_MAGENTA, VGA_BLACK);
    }
    {
        char abuf[40];
        char ans[56];
        kmath::format_double(g_ans, abuf, sizeof(abuf));
        ksnprintf(ans, sizeof(ans), "ans %s", abuf);
        int n = (int)strlen(ans);
        int sx = UI_DISP_X + UI_DISP_W - 1 - n;
        if (sx < UI_DISP_X + 1) sx = UI_DISP_X + 1;
        ui_text(sx, 3, ans, VGA_DARK_GREY, VGA_BLACK);
    }

    // expression line (right aligned, with a caret while editing)
    {
        char view[UI_TEXT_W + 2];
        int len = (int)strlen(g_expr);
        int start = len > UI_TEXT_W - 1 ? len - (UI_TEXT_W - 1) : 0;
        int vn = 0;
        for (int i = start; i < len && vn < UI_TEXT_W; i++) view[vn++] = g_expr[i];
        if (!g_fresh && vn < UI_TEXT_W) view[vn++] = '_';
        view[vn] = 0;
        int sx = UI_DISP_X + UI_DISP_W - 1 - vn;
        if (sx < UI_DISP_X + 1) sx = UI_DISP_X + 1;
        ui_text(sx, 4, view, VGA_WHITE, VGA_BLACK);
    }

    // result line
    if (g_has_result) {
        int len = (int)strlen(g_result);
        int start = len > UI_TEXT_W ? len - UI_TEXT_W : 0;
        int shown = len - start;
        int sx = UI_DISP_X + UI_DISP_W - 1 - shown;
        if (sx < UI_DISP_X + 1) sx = UI_DISP_X + 1;
        ui_text(sx, 5, g_result + start,
                g_result_ok ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    }

    for (int i = 0; i < UI_COUNT; i++) ui_draw_button(i);

    // status / memory footer
    if (g_status[0]) {
        ui_text(18, 22, g_status, g_status_ok ? VGA_LIGHT_GREEN : VGA_LIGHT_RED, VGA_BLACK);
    } else {
        char mbuf[40];
        char line[64];
        kmath::format_double(g_memory, mbuf, sizeof(mbuf));
        ksnprintf(line, sizeof(line), "memory: %s", mbuf);
        ui_text(18, 22, line, VGA_DARK_GREY, VGA_BLACK);
    }
    ui_text(2, 24,
            "Arrows: move   Enter: press   type expression + Enter: evaluate   Esc: quit",
            VGA_DARK_GREY, VGA_BLACK);
}

bool ui_token_continues(const char* token) {
    if (!token || !token[0]) return false;
    char c = token[0];
    return c == ')' || c == '+' || c == '-' || c == '*' || c == '/' ||
           c == '^' || c == '%' || c == '!';
}

void ui_insert(const char* token) {
    if (g_fresh) {
        if (ui_token_continues(token)) strcpy(g_expr, "ans");
        else g_expr[0] = 0;
        g_fresh = false;
    }
    int n = (int)strlen(g_expr);
    int m = (int)strlen(token);
    if (n + m >= (int)sizeof(g_expr)) {
        ui_status("expression is full", false);
        return;
    }
    strcpy(g_expr + n, token);
    g_status[0] = 0;
}

void ui_backspace() {
    int n = (int)strlen(g_expr);
    if (n == 0) { ui_status("nothing to delete", false); return; }
    g_expr[n - 1] = 0;
    g_fresh = false;
    g_status[0] = 0;
}

void ui_clear() {
    g_expr[0] = 0;
    g_result[0] = 0;
    g_has_result = false;
    g_fresh = false;
    ui_status("cleared", true);
}

void ui_evaluate() {
    if (!g_expr[0]) { ui_status("expression is empty", false); return; }

    double v;
    const char* error = nullptr;
    if (!eval_expression(g_expr, &v, &error)) {
        ksnprintf(g_result, sizeof(g_result), "error: %s",
                  error ? error : "invalid expression");
        g_has_result = true;
        g_result_ok = false;
        ui_status("edit the expression or press C", false);
        ui_log(g_result);
        return;
    }

    g_ans = v;
    char num[64];
    kmath::format_double(v, num, sizeof(num));
    ksnprintf(g_result, sizeof(g_result), "= %s", num);
    g_has_result = true;
    g_result_ok = true;
    g_fresh = true;
    add_history(g_expr, num);
    g_status[0] = 0;

    char line[192];
    ksnprintf(line, sizeof(line), "%s = %s", g_expr, num);
    ui_log(line);
}

void ui_mplus() {
    if (!g_expr[0]) { ui_status("nothing to add to memory", false); return; }

    double v;
    const char* error = nullptr;
    if (!eval_expression(g_expr, &v, &error)) {
        ui_status(error ? error : "invalid expression", false);
        return;
    }
    g_memory += v;

    char a[40], b[40], msg[96];
    kmath::format_double(v, a, sizeof(a));
    kmath::format_double(g_memory, b, sizeof(b));
    ksnprintf(msg, sizeof(msg), "M+ %s   memory = %s", a, b);
    ui_status(msg, true);
    ui_log(msg);
}

void ui_mr() {
    char buf[48];
    kmath::format_double(g_memory, buf, sizeof(buf));
    ui_insert(buf);
    ui_status("memory recalled", true);
}

void ui_mc() {
    g_memory = 0;
    ui_status("memory cleared", true);
}

void ui_toggle_deg() {
    g_degrees = !g_degrees;
    ui_status(g_degrees ? "angle mode: degrees" : "angle mode: radians", true);
    ui_log(g_degrees ? "angle mode: degrees" : "angle mode: radians");
}

void ui_activate(int index) {
    const UiButton& b = UI_BUTTONS[index];
    switch (b.action) {
    case UI_INSERT:  ui_insert(b.token); break;
    case UI_CLEAR:   ui_clear(); break;
    case UI_DEL:     ui_backspace(); break;
    case UI_EQUALS:  ui_evaluate(); break;
    case UI_ANS:     ui_insert("ans"); break;
    case UI_MPLUS:   ui_mplus(); break;
    case UI_MR:      ui_mr(); break;
    case UI_MC:      ui_mc(); break;
    case UI_DEG:     ui_toggle_deg(); break;
    }
    g_direct = false;
}

// Reads a key, translating ANSI escape sequences from a serial terminal into
// the same special-key bytes the PS/2 driver produces.
int ui_read_key() {
    int c = (u8)keyboard::getchar();
    if (c != 27) return c;

    char a;
    time::busy_wait_ms(20);           // give a serial escape sequence time to arrive
    if (!keyboard::try_getchar(&a)) return 27;
    if (a != '[' && a != 'O') {
        keyboard::unget_char(a);       // lone Esc: put the following key back
        return 27;
    }

    char b = keyboard::getchar();
    switch (b) {
    case 'A': return keyboard::KEY_UP;
    case 'B': return keyboard::KEY_DOWN;
    case 'C': return keyboard::KEY_RIGHT;
    case 'D': return keyboard::KEY_LEFT;
    case 'H': return keyboard::KEY_HOME;
    case 'F': return keyboard::KEY_END;
    default: break;
    }

    if (b >= '0' && b <= '9') {
        char t = keyboard::getchar();
        if (t == '~') {
            switch (b) {
            case '3': return keyboard::KEY_DELETE;
            case '5': return keyboard::KEY_PGUP;
            case '6': return keyboard::KEY_PGDN;
            default: break;
            }
        }
    }
    return 0;
}

int calculator_ui() {
    g_expr[0] = 0;
    g_result[0] = 0;
    g_status[0] = 0;
    g_has_result = false;
    g_result_ok = true;
    g_status_ok = true;
    g_fresh = false;
    g_direct = false;
    g_focus = UI_START_AT;
    g_ui_log_count = 0;

    u32 width = console::vga_width();
    u32 height = console::vga_height();
    size_t cell_count = (size_t)width * height;
    u32 saved_row = console::vga_row();
    u32 saved_col = console::vga_col();
    u16* saved = (u16*)heap::kmalloc(cell_count * sizeof(u16));
    if (saved) memcpy(saved, console::vga_buffer(), cell_count * sizeof(u16));

    vga::enable_cursor(0x20, 0x00);   // hide the hardware cursor
    serial::write("\033[2J\033[H");   // clear the serial terminal
    g_mirror_ready = false;

    for (;;) {
        ui_draw();
        ui_mirror();
        int key = ui_read_key();

        if (key == 27 || key == 3) break;      // Esc / Ctrl-C leave the UI

        if (key == keyboard::KEY_LEFT) {
            g_focus = (g_focus % UI_COLS == 0) ? g_focus + UI_COLS - 1 : g_focus - 1;
            g_direct = false;
        } else if (key == keyboard::KEY_RIGHT) {
            g_focus = (g_focus % UI_COLS == UI_COLS - 1) ? g_focus - UI_COLS + 1 : g_focus + 1;
            g_direct = false;
        } else if (key == keyboard::KEY_UP) {
            g_focus = (g_focus >= UI_COLS) ? g_focus - UI_COLS
                                           : g_focus + UI_COLS * (UI_ROWS - 1);
            g_direct = false;
        } else if (key == keyboard::KEY_DOWN) {
            g_focus = (g_focus + UI_COLS < UI_COUNT) ? g_focus + UI_COLS
                                                     : g_focus - UI_COLS * (UI_ROWS - 1);
            g_direct = false;
        } else if (key == keyboard::KEY_HOME) {
            g_focus = 0;
            g_direct = false;
        } else if (key == keyboard::KEY_END) {
            g_focus = UI_COUNT - 1;
            g_direct = false;
        } else if (key == '\n') {
            if (g_direct && g_expr[0]) ui_evaluate();
            else ui_activate(g_focus);
            g_direct = false;
        } else if (key == '\b' || key == 127 || key == keyboard::KEY_DELETE) {
            ui_backspace();
            g_direct = false;
        } else if (key >= 32 && key < 127) {
            if (key == '=') {
                ui_evaluate();
                g_direct = false;
            } else {
                char text[2] = {(char)key, 0};
                ui_insert(text);
                g_direct = true;
            }
        }
    }

    if (saved) {
        memcpy(console::vga_buffer(), saved, cell_count * sizeof(u16));
        heap::kfree(saved);
        vga::cursor_row = saved_row;
        vga::cursor_col = saved_col;
    } else {
        console::clear();
    }
    vga::enable_cursor(14, 15);
    vga::update_cursor();

    // Re-render the restored screen on the serial terminal, then replay the
    // buffered results at the original cursor position.
    serial::write("\033[2J\033[H");
    g_mirror_ready = false;
    ui_mirror();
    ui_serial_goto(saved ? (int)saved_col : 0, saved ? (int)saved_row : 0);

    console::set_color(VGA_LIGHT_CYAN, VGA_BLACK);
    for (u32 i = 0; i < g_ui_log_count; i++) kprintf("[calc-ui] %s\n", g_ui_log[i]);
    kprintf("calculator UI closed\n");
    console::reset_color();
    return 0;
}

} // namespace

namespace apps {

int calculator_main(int argc, char** argv) {
    bool want_ui = false;
    bool want_repl = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "ui") == 0 || strcmp(argv[i], "tui") == 0 ||
            strcmp(argv[i], "gui") == 0 || strcmp(argv[i], "-u") == 0) {
            want_ui = true;
        } else if (strcmp(argv[i], "repl") == 0 || strcmp(argv[i], "-r") == 0) {
            want_repl = true;
        } else {
            kprintf("usage: calc [ui|repl]\n");
            return 0;
        }
    }

    if (want_ui) return calculator_ui();
    if (!want_repl) {
        console::set_color(VGA_DARK_GREY, VGA_BLACK);
        kprintf("tip: run 'calc ui' for the full-screen arrow-key keypad\n");
        console::reset_color();
    }
    return calculator_repl();
}

} // namespace apps
